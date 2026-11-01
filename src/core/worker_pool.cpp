#include "core/worker_pool.hpp"

#include <stdexcept>

namespace wb {

WorkerPool::WorkerPool(int workers) {
    if (workers < 1) throw std::invalid_argument("worker pool needs at least 1 worker");
    threads_.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { worker_loop(); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    work_ready_.notify_all();
    for (auto& t : threads_) t.join();
}

void WorkerPool::parallel_for(std::size_t count, const std::function<void(std::size_t)>& body) {
    if (count == 0) return;
    std::unique_lock<std::mutex> lock(mutex_);
    body_ = &body;
    count_ = count;
    next_.store(0, std::memory_order_relaxed);
    finished_workers_ = 0;
    first_error_ = nullptr;
    ++generation_;
    work_ready_.notify_all();

    work_done_.wait(lock, [this] { return finished_workers_ == size(); });
    body_ = nullptr;
    if (first_error_) std::rethrow_exception(first_error_);
}

void WorkerPool::worker_loop() {
    std::uint64_t seen_generation = 0;
    for (;;) {
        const std::function<void(std::size_t)>* body = nullptr;
        std::size_t count = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_ready_.wait(lock, [&] { return stopping_ || generation_ != seen_generation; });
            if (stopping_) return;
            seen_generation = generation_;
            body = body_;
            count = count_;
        }

        try {
            for (std::size_t i = next_.fetch_add(1); i < count; i = next_.fetch_add(1)) (*body)(i);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!first_error_) first_error_ = std::current_exception();
            next_.store(count);  // stop the other workers picking up new indices
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (++finished_workers_ == size()) work_done_.notify_one();
    }
}

}  // namespace wb
