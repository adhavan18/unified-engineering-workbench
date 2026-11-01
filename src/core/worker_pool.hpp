#pragma once
// a fixed-size pool of std::threads that runs one parallel_for at a time.
//
// threads are created once in the constructor, so thread start-up cost stays
// out of benchmark timings. work is handed out one index at a time through an
// atomic counter (dynamic scheduling), which balances cases of uneven cost.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wb {

class WorkerPool {
public:
    explicit WorkerPool(int workers);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int size() const { return static_cast<int>(threads_.size()); }

    // calls body(i) for every i in [0, count) on the pool threads and blocks
    // until all of them finish. the first exception thrown by body is rethrown.
    void parallel_for(std::size_t count, const std::function<void(std::size_t)>& body);

private:
    void worker_loop();

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable work_done_;

    const std::function<void(std::size_t)>* body_ = nullptr;
    std::size_t count_ = 0;
    std::atomic<std::size_t> next_{0};
    std::uint64_t generation_ = 0;
    int finished_workers_ = 0;
    bool stopping_ = false;
    std::exception_ptr first_error_;
};

}  // namespace wb
