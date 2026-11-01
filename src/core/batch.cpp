#include "core/batch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
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

    // case generation and thread start-up are not part of the timed region
    const auto cases = generate_cases(cfg.batch);
    const int elements = cfg.batch.elements;
    std::vector<std::unique_ptr<WorkerPool>> pools;
    for (int w : workers) pools.push_back(std::make_unique<WorkerPool>(w));

    // row 0 is serial, row k is workers[k-1]
    report.rows.resize(workers.size() + 1);
    report.rows[0].label = "serial";
    for (std::size_t k = 0; k < workers.size(); ++k) {
        report.rows[k + 1].label = "parallel";
        report.rows[k + 1].workers = workers[k];
    }

    const std::size_t configs = report.rows.size();
    const std::size_t total_runs = static_cast<std::size_t>(cfg.warmups + cfg.repetitions) * configs;
    std::size_t runs_done = 0;
    if (progress) progress(0, total_runs);  // first real tick only arrives after a full run

    using clock = std::chrono::steady_clock;
    BatchOutput reference;
    // runs one configuration once; returns false if cancelled
    auto run_once = [&](std::size_t k, bool measured, bool first) {
        const auto t0 = clock::now();
        BatchOutput out = k == 0 ? run_serial(cases, elements, cancel)
                                 : run_parallel(cases, elements, *pools[k - 1], cancel);
        const double t = std::chrono::duration<double>(clock::now() - t0).count();
        if (out.cancelled) return false;
        if (measured) report.rows[k].times_s.push_back(t);
        // the first run of each config is kept for the agreement check
        if (first && k == 0) reference = std::move(out);
        else if (first) report.rows[k].max_abs_diff_C = max_abs_difference(reference, out);
        if (progress) progress(++runs_done, total_runs);
        return true;
    };

    // configurations are interleaved inside every repetition (serial, p1, p2, ...)
    // rather than run as blocks. on laptops the turbo budget runs out after a few
    // seconds of load; blocks would give the first config the fast clock and every
    // later config the throttled one, biasing the speedup.
    const int rounds = cfg.warmups + cfg.repetitions;
    for (int r = 0; r < rounds; ++r) {
        for (std::size_t k = 0; k < configs; ++k) {
            if (!run_once(k, r >= cfg.warmups, r == 0)) {
                report.cancelled = true;
                return report;
            }
        }
    }

    const double t_serial = median(report.rows[0].times_s);
    for (auto& row : report.rows) {
        row.median_s = median(row.times_s);
        row.speedup = row.median_s > 0.0 ? t_serial / row.median_s : 0.0;
        row.efficiency = row.speedup / row.workers;
        if (row.label == "parallel" && !(row.max_abs_diff_C <= report.agreement_limit_C)) {
            report.agreement_ok = false;
        }
    }
    return report;
}

}  // namespace wb::batch
