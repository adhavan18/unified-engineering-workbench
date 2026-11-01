// experiment: is the pool(1) vs serial gap an ordering / turbo-budget effect?
// alternates serial and pool(1) runs so both see the same thermal state.
#include <chrono>
#include <cstdio>
#include "core/batch.hpp"

using namespace wb;

int main() {
    batch::BatchConfig cfg;
    cfg.cases = 400;
    const auto cases = batch::generate_cases(cfg);
    WorkerPool pool(1);
    auto t = [](auto&& fn) {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    for (int r = 0; r < 8; ++r) {
        double s = t([&] { batch::run_serial(cases, 20000); });
        double p = t([&] { batch::run_parallel(cases, 20000, pool); });
        std::printf("round %d  serial %.3f s  pool(1) %.3f s  ratio %.2f\n", r, s, p, p / s);
    }
}
