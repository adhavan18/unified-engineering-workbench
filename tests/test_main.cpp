#include <chrono>
#include <cstring>
#include <exception>

#include "test_framework.hpp"

// usage: wb_tests [filter]   runs every test whose "suite.name" contains filter
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (const auto& c : wbtest::registry()) {
        const std::string full = std::string(c.suite) + "." + c.name;
        if (full.find(filter) == std::string::npos) continue;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            c.fn();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("[pass] %-55s %8.1f ms\n", full.c_str(), ms);
            ++passed;
        } catch (const wbtest::Failure& f) {
            std::printf("[FAIL] %s\n       %s\n", full.c_str(), f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s\n       unexpected exception: %s\n", full.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
