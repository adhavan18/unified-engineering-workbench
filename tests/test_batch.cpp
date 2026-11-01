#include <atomic>
#include <stdexcept>

#include "core/batch.hpp"
#include "core/worker_pool.hpp"
#include "test_framework.hpp"

using namespace wb::batch;

TEST(batch, generator_is_reproducible_for_a_seed) {
    BatchConfig cfg;
    cfg.cases = 50;
    const auto a = generate_cases(cfg);
    const auto b = generate_cases(cfg);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].conductivity_W_mK == b[i].conductivity_W_mK);
        CHECK(a[i].heat_gen_W_m3 == b[i].heat_gen_W_m3);
    }
    cfg.seed += 1;
    const auto c = generate_cases(cfg);
    CHECK(c[0].conductivity_W_mK != a[0].conductivity_W_mK);
}

TEST(batch, generated_k_and_q_are_positive_and_varied) {
    BatchConfig cfg;
    cfg.cases = 500;
    const auto cases = generate_cases(cfg);
    double kmin = 1e300, kmax = 0, qmin = 1e300, qmax = 0;
    for (const auto& c : cases) {
        CHECK(c.conductivity_W_mK >= cfg.k_min_W_mK && c.conductivity_W_mK <= cfg.k_max_W_mK);
        CHECK(c.heat_gen_W_m3 >= cfg.q_min_W_m3 && c.heat_gen_W_m3 <= cfg.q_max_W_m3);
        kmin = std::min(kmin, c.conductivity_W_mK);
        kmax = std::max(kmax, c.conductivity_W_mK);
        qmin = std::min(qmin, c.heat_gen_W_m3);
        qmax = std::max(qmax, c.heat_gen_W_m3);
    }
    CHECK(kmax - kmin > 300.0);
    CHECK(qmax - qmin > 8.0e4);
}

TEST(batch, serial_and_parallel_agree_for_several_worker_counts) {
    BatchConfig cfg;
    cfg.cases = 37;  // deliberately not a multiple of the worker count
    cfg.elements = 500;
    const auto cases = generate_cases(cfg);
    const BatchOutput serial = run_serial(cases, cfg.elements);
    for (int w : {1, 2, 3, 4, 7}) {
        const BatchOutput par = run_parallel(cases, cfg.elements, w);
        CHECK(par.temp_C.size() == serial.temp_C.size());
        CHECK(max_abs_difference(serial, par) <= 1e-8);
    }
}

TEST(batch, parallel_results_keep_input_order) {
    BatchConfig cfg;
    cfg.cases = 20;
    cfg.elements = 50;
    const auto cases = generate_cases(cfg);
    const BatchOutput par = run_parallel(cases, cfg.elements, 4);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto single = wb::fea::solve(cases[i], cfg.elements);
        CHECK(par.temp_C[i] == single.temp_C);
    }
}

TEST(batch, cancellation_stops_early_and_is_reported) {
    BatchConfig cfg;
    cfg.cases = 100;
    cfg.elements = 100;
    const auto cases = generate_cases(cfg);
    std::atomic<bool> cancel{true};
    CHECK(run_serial(cases, cfg.elements, &cancel).cancelled);
    CHECK(run_parallel(cases, cfg.elements, 2, &cancel).cancelled);
}

TEST(batch, pool_is_reusable_and_rethrows_errors) {
    wb::WorkerPool pool(3);
    std::atomic<int> sum{0};
    for (int round = 0; round < 5; ++round) pool.parallel_for(100, [&](std::size_t i) { sum += static_cast<int>(i); });
    CHECK(sum.load() == 5 * 4950);
    CHECK_THROWS_WITH(pool.parallel_for(10, [](std::size_t i) { if (i == 3) throw std::runtime_error("boom"); }),
                      std::runtime_error, "boom");
    pool.parallel_for(4, [&](std::size_t) { ++sum; });  // still usable after an error
    CHECK(sum.load() == 5 * 4950 + 4);
}

TEST(batch, benchmark_reports_agreement_and_caps_workers) {
    BenchConfig cfg;
    cfg.batch.cases = 16;
    cfg.batch.elements = 200;
    cfg.workers = {1, 2, 64};
    cfg.repetitions = 3;
    const BenchReport r = run_benchmark(cfg, 2);
    CHECK(r.skipped_workers.size() == 1 && r.skipped_workers[0] == 64);
    CHECK(r.rows.size() == 3);  // serial + 1 + 2
    CHECK(r.rows[0].label == "serial");
    CHECK(r.agreement_ok);
    for (const auto& row : r.rows) {
        CHECK(row.times_s.size() == 3);
        CHECK(row.median_s > 0.0);
    }
}

TEST(batch, run_callback_reports_every_run_in_interleaved_order) {
    BenchConfig cfg;
    cfg.batch.cases = 6;
    cfg.batch.elements = 50;
    cfg.workers = {1, 2};
    cfg.warmups = 1;
    cfg.repetitions = 2;
    std::vector<std::size_t> rows;
    int measured = 0;
    run_benchmark(cfg, 4, nullptr, {}, [&](std::size_t row, int, int, bool m, double t) {
        rows.push_back(row);
        measured += m ? 1 : 0;
        CHECK(t >= 0.0);
    });
    CHECK(rows.size() == 9);  // 3 configs x (1 warm-up + 2 reps)
    CHECK(measured == 6);
    CHECK(rows[0] == 0 && rows[1] == 1 && rows[2] == 2 && rows[3] == 0);  // round-robin, not blocks
}

TEST(batch, single_cpu_machine_still_runs_the_parallel_path) {
    BenchConfig cfg;
    cfg.batch.cases = 4;
    cfg.batch.elements = 50;
    cfg.repetitions = 1;
    const BenchReport r = run_benchmark(cfg, 1);
    CHECK(r.single_cpu);
    CHECK(r.rows.size() == 2 && r.rows[1].label == "parallel" && r.rows[1].workers == 1);
}

TEST(batch, invalid_configs_are_rejected) {
    BatchConfig cfg;
    cfg.cases = 0;
    CHECK(!validate(cfg).empty());
    cfg = {};
    cfg.k_min_W_mK = -1.0;
    CHECK(!validate(cfg).empty());
    cfg = {};
    cfg.cases = 100000;
    cfg.elements = 1000000;
    CHECK(validate(cfg).find("2 GB") != std::string::npos);
    BenchConfig b;
    b.repetitions = 0;
    CHECK(!validate(b).empty());
}

TEST(batch, median_of_odd_and_even_counts) {
    CHECK(median({5, 1, 3}) == 3);
    CHECK(median({4, 1, 3, 2}) == 2.5);
}
