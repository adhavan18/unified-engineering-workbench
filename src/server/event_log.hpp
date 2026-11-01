#pragma once
// in-memory activity log shown in the ui, mirrored to the console window.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace wb::server {

class EventLog {
public:
    void add(const std::string& level, const std::string& message) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        localtime_s(&tm, &t);
        char stamp[32];
        std::snprintf(stamp, sizeof stamp, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      static_cast<int>(ms));
        std::string line = std::string(stamp) + " [" + level + "] " + message;

        std::lock_guard<std::mutex> lock(mutex_);
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        lines_.push_back(std::move(line));
        if (lines_.size() > 500) lines_.pop_front();
    }

    std::vector<std::string> recent(std::size_t n) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t start = lines_.size() > n ? lines_.size() - n : 0;
        return {lines_.begin() + static_cast<std::ptrdiff_t>(start), lines_.end()};
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
};

}  // namespace wb::server
