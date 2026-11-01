// experiment: is the 1-worker slowdown about *which thread* runs the loop?
// runs the identical serial batch on the main thread and on a fresh std::thread,
// alternating, and also on a thread pinned to logical cpu 0 (a p-core on this machine).
#include <chrono>
#include <cstdio>
#include <thread>
#include <windows.h>
#include "core/batch.hpp"

using namespace wb;

static double time_run(const std::vector<fea::RodProblem>& cases) {
    auto t0 = std::chrono::steady_clock::now();
    auto out = batch::run_serial(cases, 20000);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    batch::BatchConfig cfg;
    cfg.cases = 400;
    const auto cases = batch::generate_cases(cfg);
    time_run(cases);  // warm-up
    for (int r = 0; r < 4; ++r) {
        double main_t = time_run(cases);
        double thread_t = 0, pinned_t = 0;
        std::thread([&] { thread_t = time_run(cases); }).join();
        std::thread([&] {
            SetThreadAffinityMask(GetCurrentThread(), 1);
            pinned_t = time_run(cases);
        }).join();
        DWORD cpu_main = GetCurrentProcessorNumber();
        std::printf("main %.3f s (cpu %lu) | new thread %.3f s | new thread pinned to cpu0 %.3f s\n", main_t, cpu_main,
                    thread_t, pinned_t);
    }
}
