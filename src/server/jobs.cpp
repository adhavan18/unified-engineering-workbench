#include "server/jobs.hpp"

#include <chrono>
#include <stdexcept>
#include <vector>

#include "core/json.hpp"

namespace wb::server {

namespace {

double now_ms() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

constexpr std::size_t kMaxJobs = 40;

}  // namespace

JobManager::~JobManager() {
    std::vector<std::shared_ptr<Job>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, job] : jobs_) all.push_back(job);
    }
    for (auto& job : all) {
        job->cancel.store(true);
        if (job->thread.joinable()) job->thread.join();
    }
}

std::string JobManager::start(const std::string& kind, Work work) {
    auto job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prune_locked();
        job->id = kind + "-" + std::to_string(next_id_++);
        job->kind = kind;
        job->started_ms = now_ms();
        jobs_[job->id] = job;
    }

    std::lock_guard<std::mutex> job_lock(job->mutex);  // thread handle is written under the job lock
    job->thread = std::thread([job, work = std::move(work)] {
        JobContext ctx(
            job->cancel,
            [job](double fraction, const std::string& message) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->progress = fraction;
                job->message = message;
            },
            [job](const std::string& json) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->partial_json = json;
            });
        try {
            JobOutput out = work(ctx);
            std::lock_guard<std::mutex> lock(job->mutex);
            if (job->cancel.load()) {
                job->state = State::cancelled;
                job->message = "cancelled; no partial results were kept";
            } else {
                job->output = std::move(out);  // published in one step, under the lock
                job->state = State::done;
                job->progress = 1.0;
                job->message = "done";
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->state = State::failed;
            job->error = e.what();
            job->message = "failed";
        }
        std::lock_guard<std::mutex> lock(job->mutex);
        job->finished_ms = now_ms();
    });
    return job->id;
}

bool JobManager::running(const std::string& kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, job] : jobs_) {
        std::lock_guard<std::mutex> job_lock(job->mutex);
        if (job->kind == kind && job->state == State::running) return true;
    }
    return false;
}

std::optional<std::string> JobManager::status_json(const std::string& id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return std::nullopt;
        job = it->second;
    }
    std::lock_guard<std::mutex> lock(job->mutex);
    static const char* names[] = {"running", "done", "failed", "cancelled"};
    JsonWriter w;
    w.begin_object();
    w.field("id", job->id).field("kind", job->kind).field("state", names[static_cast<int>(job->state)]);
    w.field("progress", job->progress).field("message", job->message);
    const double end = job->finished_ms > 0 ? job->finished_ms : now_ms();
    w.field("elapsed_ms", end - job->started_ms);
    if (job->state == State::failed) w.field("error", job->error);
    if (job->state == State::running && !job->partial_json.empty()) w.key("partial").raw(job->partial_json);
    if (job->state == State::done) {
        w.key("result").raw(job->output.result_json.empty() ? "null" : job->output.result_json);
        w.key("exports").begin_array();
        for (const auto& [name, csv] : job->output.exports) w.value(name);
        w.end_array();
    }
    w.end_object();
    return w.str();
}

bool JobManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    it->second->cancel.store(true);
    std::lock_guard<std::mutex> job_lock(it->second->mutex);
    if (it->second->state == State::running) it->second->message = "cancelling...";
    return true;
}

std::optional<std::string> JobManager::export_csv(const std::string& id, const std::string& name) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return std::nullopt;
        job = it->second;
    }
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->state != State::done) return std::nullopt;
    auto it = job->output.exports.find(name);
    if (it == job->output.exports.end()) return std::nullopt;
    return it->second;
}

void JobManager::prune_locked() {
    if (jobs_.size() < kMaxJobs) return;
    for (auto it = jobs_.begin(); it != jobs_.end() && jobs_.size() >= kMaxJobs;) {
        auto job = it->second;
        bool finished;
        {
            std::lock_guard<std::mutex> job_lock(job->mutex);
            finished = job->state != State::running && job->finished_ms > 0;
        }
        if (finished) {
            if (job->thread.joinable()) job->thread.join();
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace wb::server
