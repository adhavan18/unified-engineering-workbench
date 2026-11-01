#pragma once
// batch of independent rod solves, run serially or on a worker pool, plus the
// timing harness for task d.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/fea1d.hpp"
#include "core/worker_pool.hpp"

namespace wb::batch {

struct BatchConfig {
    std::uint64_t seed = 20261101;
    int cases = 400;
    int elements = 20'000;
    double k_min_W_mK = 1.0;
    double k_max_W_mK = 400.0;
    double q_min_W_m3 = 1.0e3;
    double q_max_W_m3 = 1.0e5;
};

struct BatchOutput {
    std::vector<std::vector<double>> temp_C;  // one vector per case, in input order
    bool cancelled = false;
};

using Progress = std::function<void(std::size_t done, std::size_t total)>;

std::string validate(const BatchConfig& cfg);

// same seed -> same cases on every platform: the [0,1) mapping is done by hand
// because std::uniform_real_distribution is implementation-defined.
std::vector<fea::RodProblem> generate_cases(const BatchConfig& cfg);

BatchOutput run_serial(const std::vector<fea::RodProblem>& cases, int elements,
                       const std::atomic<bool>* cancel = nullptr);

// uses an existing pool so thread start-up stays outside any timing.
BatchOutput run_parallel(const std::vector<fea::RodProblem>& cases, int elements,
                         WorkerPool& pool, const std::atomic<bool>* cancel = nullptr);

// convenience overload that builds a pool of `workers` threads for one run.
BatchOutput run_parallel(const std::vector<fea::RodProblem>& cases, int elements, int workers,
                         const std::atomic<bool>* cancel = nullptr);

double max_abs_difference(const BatchOutput& a, const BatchOutput& b);

// ---- benchmark -------------------------------------------------------------

struct BenchConfig {
    BatchConfig batch;
    std::vector<int> workers{1, 2, 4};
    int warmups = 1;
    int repetitions = 5;
};

struct BenchRow {
    std::string label;  // "serial" or "parallel"
    int workers = 1;
    std::vector<double> times_s;
    double median_s = 0.0;
    double speedup = 0.0;     // T_serial / T_p
    double efficiency = 0.0;  // speedup / p
    double max_abs_diff_C = 0.0;
};

struct BenchReport {
    BenchConfig config;
    std::vector<int> skipped_workers;  // requested counts above the logical cpu count
    std::vector<BenchRow> rows;        // serial first, then each worker count
    double agreement_limit_C = 1e-8;
    bool agreement_ok = true;
    bool single_cpu = false;
    bool cancelled = false;
};

std::string validate(const BenchConfig& cfg);

// runs warm-up + measured repetitions for serial and each worker count.
// `progress` is called after every run; `cancel` is polled between cases.
BenchReport run_benchmark(const BenchConfig& cfg, int logical_cpus,
                          const std::atomic<bool>* cancel = nullptr,
                          const Progress& progress = {});

double median(std::vector<double> values);

}  // namespace wb::batch
