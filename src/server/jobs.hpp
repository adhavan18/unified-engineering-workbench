#pragma once
// background jobs for long computations, so http handlers return at once and
// the ui stays responsive.
//
// a job publishes its result only when its work function returns normally.
// a cancelled or failed job never exposes partial output, so a cancel cannot
// corrupt results that were already shown or exported.

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace wb::server {

class JobContext {
public:
    explicit JobContext(std::atomic<bool>& cancel, std::function<void(double, const std::string&)> report)
        : cancel_(cancel), report_(std::move(report)) {}
    bool cancelled() const { return cancel_.load(std::memory_order_relaxed); }
    const std::atomic<bool>* cancel_flag() const { return &cancel_; }
    void progress(double fraction, const std::string& message) { report_(fraction, message); }

private:
    std::atomic<bool>& cancel_;
    std::function<void(double, const std::string&)> report_;
};

struct JobOutput {
    std::string result_json;
    std::map<std::string, std::string> exports;  // export name -> csv text
};

class JobManager {
public:
    using Work = std::function<JobOutput(JobContext&)>;

    JobManager() = default;
    ~JobManager();
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    std::string start(const std::string& kind, Work work);
    bool running(const std::string& kind);
    std::optional<std::string> status_json(const std::string& id);
    bool cancel(const std::string& id);
    std::optional<std::string> export_csv(const std::string& id, const std::string& name);

private:
    enum class State { running, done, failed, cancelled };
    struct Job {
        std::string id, kind;
        std::atomic<bool> cancel{false};
        std::mutex mutex;  // guards everything below
        State state = State::running;
        double progress = 0.0;
        std::string message = "queued";
        std::string error;
        JobOutput output;
        double started_ms = 0.0, finished_ms = 0.0;
        std::thread thread;
    };

    void prune_locked();

    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    int next_id_ = 1;
};

}  // namespace wb::server
