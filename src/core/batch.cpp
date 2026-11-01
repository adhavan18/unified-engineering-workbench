#include "core/batch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>

namespace wb::batch {

namespace {

// 53 random bits -> uniform double in [0, 1), identical on every standard library.
double unit_interval(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}

bool is_cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

}  // namespace

std::string validate(const BatchConfig& cfg) {
    if (cfg.cases < 1 || cfg.cases > 100'000) return "case count must be between 1 and 100,000";
    if (cfg.elements < 1 || cfg.elements > 1'000'000) {
        return "mesh size must be between 1 and 1,000,000 elements per case";
    }
    // the agreement check keeps two full result sets in memory
    const double bytes = 2.0 * cfg.cases * (cfg.elements + 1.0) * sizeof(double);
    if (bytes > 2.0e9) {
        return "cases x elements is too large: the agreement check would need over 2 GB. "
               "lower the case count or mesh size";
    }
    if (!(cfg.k_min_W_mK > 0.0) || !(cfg.k_max_W_mK >= cfg.k_min_W_mK)) {
        return "k range must be positive with k_min <= k_max";
    }
    if (!(cfg.q_min_W_m3 > 0.0) || !(cfg.q_max_W_m3 >= cfg.q_min_W_m3)) {
        return "q range must be positive with q_min <= q_max";
    }
    return {};
}

std::vector<fea::RodProblem> generate_cases(const BatchConfig& cfg) {
    if (auto why = validate(cfg); !why.empty()) throw std::invalid_argument(why);
    std::mt19937_64 rng(cfg.seed);
    std::vector<fea::RodProblem> cases(static_cast<std::size_t>(cfg.cases));
    for (auto& c : cases) {
        c.conductivity_W_mK = cfg.k_min_W_mK + (cfg.k_max_W_mK - cfg.k_min_W_mK) * unit_interval(rng);
        c.heat_gen_W_m3 = cfg.q_min_W_m3 + (cfg.q_max_W_m3 - cfg.q_min_W_m3) * unit_interval(rng);
        // geometry and boundary values stay at the task c defaults
    }
    return cases;
}

BatchOutput run_serial(const std::vector<fea::RodProblem>& cases, int elements,
                       const std::atomic<bool>* cancel) {
    BatchOutput out;
    out.temp_C.resize(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (is_cancelled(cancel)) { out.cancelled = true; break; }
        out.temp_C[i] = fea::solve(cases[i], elements).temp_C;
    }
    return out;
}

BatchOutput run_parallel(const std::vector<fea::RodProblem>& cases, int elements,
                         WorkerPool& pool, const std::atomic<bool>* cancel) {
    BatchOutput out;
    out.temp_C.resize(cases.size());  // each worker writes only its own slot -> input order kept
    std::atomic<bool> stopped{false};
    pool.parallel_for(cases.size(), [&](std::size_t i) {
        if (is_cancelled(cancel)) { stopped.store(true); return; }
        out.temp_C[i] = fea::solve(cases[i], elements).temp_C;
    });
    out.cancelled = stopped.load();
    return out;
}

BatchOutput run_parallel(const std::vector<fea::RodProblem>& cases, int elements, int workers,
                         const std::atomic<bool>* cancel) {
    WorkerPool pool(workers);
    return run_parallel(cases, elements, pool, cancel);
}

double max_abs_difference(const BatchOutput& a, const BatchOutput& b) {
    if (a.temp_C.size() != b.temp_C.size()) return INFINITY;
    double worst = 0.0;
    for (std::size_t c = 0; c < a.temp_C.size(); ++c) {
        const auto& ta = a.temp_C[c];
        const auto& tb = b.temp_C[c];
        if (ta.size() != tb.size()) return INFINITY;
        for (std::size_t i = 0; i < ta.size(); ++i) worst = std::max(worst, std::abs(ta[i] - tb[i]));
    }
    return worst;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

std::string validate(const BenchConfig& cfg) {
    if (auto why = validate(cfg.batch); !why.empty()) return why;
    if (cfg.workers.empty()) return "list at least one worker count, e.g. 1,2,4";
    for (int w : cfg.workers) {
        if (w < 1 || w > 256) return "worker counts must be between 1 and 256";
    }
    if (cfg.warmups < 0 || cfg.warmups > 10) return "warm-up runs must be between 0 and 10";
    if (cfg.repetitions < 1 || cfg.repetitions > 50) return "repetitions must be between 1 and 50";
    return {};
}

BenchReport run_benchmark(const BenchConfig& cfg, int logical_cpus,
                          const std::atomic<bool>* cancel, const Progress& progress) {
    if (auto why = validate(cfg); !why.empty()) throw std::invalid_argument(why);

    BenchReport report;
    report.config = cfg;
    report.single_cpu = logical_cpus <= 1;

    std::vector<int> workers;
    for (int w : cfg.workers) {
        if (w > std::max(1, logical_cpus)) report.skipped_workers.push_back(w);
        else if (std::find(workers.begin(), workers.end(), w) == workers.end()) workers.push_back(w);
    }
    if (workers.empty()) workers.push_back(1);
    std::sort(workers.begin(), workers.end());
    report.config.workers = workers;

    // case generation is not part of the timed region
    const auto cases = generate_cases(cfg.batch);
    const int elements = cfg.batch.elements;
    const std::size_t runs_per_config = static_cast<std::size_t>(cfg.warmups + cfg.repetitions);
    const std::size_t total_runs = runs_per_config * (workers.size() + 1);
    std::size_t runs_done = 0;
    auto tick = [&] { if (progress) progress(++runs_done, total_runs); };
    if (progress) progress(0, total_runs);  // first real tick only arrives after a full run

    using clock = std::chrono::steady_clock;
    auto seconds_since = [](clock::time_point t0) {
        return std::chrono::duration<double>(clock::now() - t0).count();
    };

    // serial reference. the first warm-up (or first measured run) is kept for the
    // agreement check; later runs are timed and their output discarded.
    BenchRow serial;
    serial.label = "serial";
    BatchOutput reference;
    for (std::size_t r = 0; r < runs_per_config; ++r) {
        const auto t0 = clock::now();
        BatchOutput out = run_serial(cases, elements, cancel);
        const double t = seconds_since(t0);
        if (out.cancelled) { report.cancelled = true; return report; }
        if (r >= static_cast<std::size_t>(cfg.warmups)) serial.times_s.push_back(t);
        if (r == 0) reference = std::move(out);
        tick();
    }
    serial.median_s = median(serial.times_s);
    serial.speedup = 1.0;
    serial.efficiency = 1.0;
    report.rows.push_back(serial);

    for (int w : workers) {
        WorkerPool pool(w);  // threads start here, outside the timed region
        BenchRow row;
        row.label = "parallel";
        row.workers = w;
        for (std::size_t r = 0; r < runs_per_config; ++r) {
            const auto t0 = clock::now();
            BatchOutput out = run_parallel(cases, elements, pool, cancel);
            const double t = seconds_since(t0);
            if (out.cancelled) { report.cancelled = true; return report; }
            if (r >= static_cast<std::size_t>(cfg.warmups)) row.times_s.push_back(t);
            if (r == 0) row.max_abs_diff_C = max_abs_difference(reference, out);
            tick();
        }
        row.median_s = median(row.times_s);
        row.speedup = row.median_s > 0.0 ? serial.median_s / row.median_s : 0.0;
        row.efficiency = row.speedup / w;
        if (!(row.max_abs_diff_C <= report.agreement_limit_C)) report.agreement_ok = false;
        report.rows.push_back(row);
    }
    return report;
}

}  // namespace wb::batch
