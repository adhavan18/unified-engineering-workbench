// regenerates the evidence in results/ from the same c++ core the app uses.
//
//   wb_report all <results_dir> [cases elements]   fea + backprop + benchmark
//   wb_report fea <results_dir>
//   wb_report backprop <results_dir>
//   wb_report bench <results_dir> [cases elements]
//   wb_report filesize <results_dir> <file>...      parse-time table for big inputs

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/backprop.hpp"
#include "core/batch.hpp"
#include "core/fea1d.hpp"
#include "core/nodal_csv.hpp"
#include "core/stl.hpp"
#include "core/sysinfo.hpp"
#include "core/thermal_study.hpp"
#include "report/svg_plot.hpp"

using namespace wb;

namespace {

std::string fmt(double v, const char* spec = "%.12g") {
    char buf[48];
    std::snprintf(buf, sizeof buf, spec, v);
    return buf;
}

void save(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
    std::printf("  wrote %s\n", path.c_str());
}

const char* kColors[] = {"#dc2626", "#ea580c", "#16a34a", "#2563eb", "#7c3aed", "#0891b2"};

void run_fea(const std::string& dir) {
    std::printf("fea convergence study\n");
    const fea::RodProblem rod;

    report::Chart temp;
    temp.title = "1d rod: fe temperature vs exact (L = 1 m, k = 10 W/(m K), q = 1e4 W/m^3)";
    temp.x_label = "x (m)";
    temp.y_label = "temperature T (C)";
    temp.notes = {"bc: T(0) = 100 C, T(L) = 20 C; insulated lateral surface; two-node linear elements"};
    report::Series exact{"exact T(x)", {}, {}, "#111827", true};
    for (int i = 0; i <= 400; ++i) {
        exact.x.push_back(i / 400.0);
        exact.y.push_back(fea::exact_temperature(rod, i / 400.0));
    }

    report::Chart err;
    err.title = "mesh convergence of the max midpoint error";
    err.x_label = "element size h (m)";
    err.y_label = "max |T_fe - T_exact| (C)";
    err.log_x = err.log_y = true;
    report::Series mid{"midpoint error (fe interp.)", {}, {}, "#dc2626", false, true};
    report::Series theory{"theory q h^2 / (8 k)", {}, {}, "#6b7280", true};
    report::Series nodal{"nodal error", {}, {}, "#2563eb", false, true};

    // same study code as the app's thermal view, so results/ and the ui cannot disagree
    const fea::Study st = fea::run_study(rod, {10, 20, 40, 80}, true);
    for (std::size_t m = 0; m < st.meshes.size(); ++m) {
        const auto& r = st.meshes[m];
        const int n = r.elements;
        temp.series.push_back({"fe N = " + std::to_string(n), r.solution.x_m, r.solution.temp_C, kColors[m], false, n <= 20});
        mid.x.push_back(r.error.h_m);
        mid.y.push_back(r.error.max_midpoint_error_C);
        theory.x.push_back(r.error.h_m);
        theory.y.push_back(r.theory_midpoint_error_C);
        nodal.x.push_back(r.error.h_m);
        nodal.y.push_back(std::max(r.error.max_nodal_error_C, 1e-16));
        std::printf("  N=%-3d h=%.4f  max midpoint err=%.10f C  max nodal err=%.3e C  order=%s\n", n, r.error.h_m,
                    r.error.max_midpoint_error_C, r.error.max_nodal_error_C, m ? fmt(r.observed_order, "%.4f").c_str() : "-");
    }
    temp.series.push_back(exact);
    err.series = {mid, theory, nodal};
    err.notes = {"slope 2 on log-log axes = second-order convergence; nodal error is at round-off (1d superconvergence)"};

    save(dir + "/fea_solution_nodes.csv", fea::nodes_csv(st));
    save(dir + "/fea_midpoints.csv", fea::midpoints_csv(st));
    save(dir + "/fea_convergence.csv", fea::convergence_csv(st));
    report::write_svg(temp, dir + "/fea_temperature.svg");
    report::write_svg(err, dir + "/fea_convergence.svg");
    std::printf("  wrote fea_temperature.svg, fea_convergence.svg\n");
}

void run_backprop(const std::string& dir) {
    std::printf("backprop gradient check\n");
    const nn::Params p0;
    const auto f = nn::forward(p0, 1, 2);
    const auto ga = nn::analytic_gradients(p0, 1, 2);
    const auto gn = nn::finite_difference_gradients(p0, 1, 2, 1e-6);
    const auto p1 = nn::sgd_step(p0, ga, 0.1);
    std::printf("  forward: z=%.6f h=%.12f y_hat=%.12f J=%.12f\n", f.z, f.h, f.y_hat, f.loss);

    std::ostringstream gc;
    gc << "parameter,value_before,analytic_grad,central_diff_grad,abs_error,pass_le_1e-5,value_after\n";
    const char* names[] = {"w", "b", "v", "c"};
    const double a[] = {ga.dw, ga.db, ga.dv, ga.dc}, n[] = {gn.dw, gn.db, gn.dv, gn.dc};
    const double b[] = {p0.w, p0.b, p0.v, p0.c}, af[] = {p1.w, p1.b, p1.v, p1.c};
    for (int i = 0; i < 4; ++i) {
        const double e = std::abs(a[i] - n[i]);
        gc << names[i] << ',' << fmt(b[i]) << ',' << fmt(a[i]) << ',' << fmt(n[i]) << ',' << fmt(e, "%.3e") << ','
           << (e <= 1e-5 ? "yes" : "NO") << ',' << fmt(af[i]) << '\n';
        std::printf("  dJ/d%s analytic=%.12f central=%.12f |err|=%.2e\n", names[i], a[i], n[i], e);
    }
    save(dir + "/backprop_gradcheck.csv", gc.str());

    const auto losses = nn::train_losses(p0, 1, 2, 0.1, 101);
    std::ostringstream ls;
    ls << "step,loss\n";
    report::Series s{"loss J", {}, {}, "#2563eb"};
    for (std::size_t i = 0; i < losses.size(); ++i) {
        ls << i << ',' << fmt(losses[i]) << '\n';
        s.x.push_back(static_cast<double>(i));
        s.y.push_back(losses[i]);
    }
    save(dir + "/backprop_loss.csv", ls.str());
    report::Chart c;
    c.title = "training loss, x = 1, y = 2, learning rate 0.1";
    c.x_label = "gradient-descent step (step 1 = the handwritten update)";
    c.y_label = "loss J = 0.5 (y_hat - y)^2";
    c.log_y = true;
    c.notes = {"J(0) = " + fmt(losses[0], "%.6f") + ", J(1) = " + fmt(losses[1], "%.6f") + ", J(101) = " +
               fmt(losses.back(), "%.3e")};
    c.series = {s};
    report::write_svg(c, dir + "/backprop_loss.svg");
    std::printf("  wrote backprop_loss.svg\n");
}

void run_bench(const std::string& dir, int cases, int elements) {
    const SystemInfo sys = query_system_info();
    std::printf("benchmark on %s (%d physical / %d logical), %.1f GiB\n", sys.cpu_name.c_str(), sys.physical_cores,
                sys.logical_cpus, sys.ram_gib);
    batch::BenchConfig cfg;
    cfg.batch.cases = cases;
    cfg.batch.elements = elements;
    cfg.workers = {1, 2, 4};
    const auto r = batch::run_benchmark(cfg, sys.logical_cpus, nullptr, [](std::size_t d, std::size_t t) {
        std::printf("\r  run %zu / %zu", d, t);
        std::fflush(stdout);
    });
    std::printf("\n");

    std::ostringstream sum, raw, info;
    sum << "mode,workers,median_s,speedup,efficiency,max_abs_diff_C,rep1_s,rep2_s,rep3_s,rep4_s,rep5_s\n";
    raw << "mode,workers,repetition,time_s\n";
    report::Series measured{"measured speedup", {}, {}, "#2563eb", false, true};
    report::Series ideal{"ideal (linear)", {}, {}, "#9ca3af", true};
    for (const auto& row : r.rows) {
        sum << row.label << ',' << row.workers << ',' << fmt(row.median_s, "%.6f") << ',' << fmt(row.speedup, "%.4f") << ','
            << fmt(row.efficiency, "%.4f") << ',' << fmt(row.max_abs_diff_C, "%.3e");
        for (double t : row.times_s) sum << ',' << fmt(t, "%.6f");
        sum << '\n';
        for (std::size_t i = 0; i < row.times_s.size(); ++i) {
            raw << row.label << ',' << row.workers << ',' << i + 1 << ',' << fmt(row.times_s[i], "%.6f") << '\n';
        }
        std::printf("  %-8s p=%d  median %.4f s  speedup %.3f  efficiency %.3f  max|dT| %.1e C\n", row.label.c_str(),
                    row.workers, row.median_s, row.speedup, row.efficiency, row.max_abs_diff_C);
        if (row.label == "parallel") {
            measured.x.push_back(row.workers);
            measured.y.push_back(row.speedup);
            ideal.x.push_back(row.workers);
            ideal.y.push_back(row.workers);
        }
    }
    measured.line = true;
    save(dir + "/benchmark_summary.csv", sum.str());
    save(dir + "/benchmark_raw.csv", raw.str());

    info << "cpu: " << sys.cpu_name << "\nphysical_cores: " << sys.physical_cores << "\nlogical_cpus: " << sys.logical_cpus
         << "\nram_gib: " << fmt(sys.ram_gib, "%.1f") << "\nos: " << sys.os << "\ncompiler: " << sys.compiler
         << "\nbuild_type: " << sys.build_type << "\nbuild_flags: " << sys.build_flags
         << "\nworkload: " << cases << " independent rod solves x " << elements << " linear elements each, seed "
         << cfg.batch.seed << ", k in [1, 400] W/(m K), q in [1e3, 1e5] W/m^3"
         << "\nwarmups: " << cfg.warmups << " (first warm-up output kept for the agreement check)"
         << "\nrepetitions: " << cfg.repetitions << " measured, median reported"
         << "\ntimed region: run_serial / run_parallel only (case generation, thread start-up, file io excluded)"
         << "\nagreement: max |T_serial - T_parallel| <= " << r.agreement_limit_C << " C -> "
         << (r.agreement_ok ? "pass" : "FAIL") << '\n';
    if (!r.skipped_workers.empty()) info << "skipped worker counts above logical cpus: " << r.skipped_workers.size() << '\n';
    save(dir + "/benchmark_system.txt", info.str());

    report::Chart c;
    c.title = "batch solve speedup T1 / Tp (serial baseline)";
    c.x_label = "worker threads p";
    c.y_label = "speedup";
    c.notes = {sys.cpu_name + ", " + std::to_string(cases) + " cases x " + std::to_string(elements) + " elements, median of 5"};
    c.series = {measured, ideal};
    report::write_svg(c, dir + "/benchmark_speedup.svg");
    std::printf("  wrote benchmark_speedup.svg\n");
}

void run_filesize(const std::string& dir, int argc, char** argv, int first) {
    std::ostringstream out;
    out << "file,bytes,kind,items,parse_ms,result\n";
    for (int i = first; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        const std::string name = argv[i];
        const bool is_stl = name.size() > 4 && name.substr(name.size() - 4) == ".stl";
        const auto t0 = std::chrono::steady_clock::now();
        std::string result = "ok";
        std::size_t items = 0;
        try {
            if (is_stl) items = stl::parse_ascii(text).triangles;
            else items = csv::parse_nodal(text).size();
        } catch (const std::exception& e) {
            result = std::string("rejected: ") + e.what();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const std::string base = name.substr(name.find_last_of("/\\") + 1);
        out << base << ',' << text.size() << ',' << (is_stl ? "ascii stl" : "nodal csv") << ',' << items << ','
            << fmt(ms, "%.1f") << ",\"" << result << "\"\n";
        std::printf("  %-28s %10zu bytes  %9zu items  %8.1f ms  %s\n", base.c_str(), text.size(), items, ms, result.c_str());
    }
    save(dir + "/file_size_tests.csv", out.str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: wb_report all|fea|backprop|bench|filesize <results_dir> [...]\n");
        return 2;
    }
    const std::string cmd = argv[1], dir = argv[2];
    const int cases = argc > 3 ? std::atoi(argv[3]) : 400;
    const int elements = argc > 4 ? std::atoi(argv[4]) : 20000;
    if (cmd == "fea" || cmd == "all") run_fea(dir);
    if (cmd == "backprop" || cmd == "all") run_backprop(dir);
    if (cmd == "bench" || cmd == "all") run_bench(dir, cases, elements);
    if (cmd == "filesize") run_filesize(dir, argc, argv, 3);
    return 0;
}
