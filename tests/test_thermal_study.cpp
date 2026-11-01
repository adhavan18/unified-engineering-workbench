#include <algorithm>
#include <sstream>

#include "core/thermal_study.hpp"
#include "test_framework.hpp"

using namespace wb::fea;

namespace {
std::size_t count_lines(const std::string& s) { return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')); }
}

TEST(study, peak_of_the_brief_rod) {
    double x = 0, t = 0;
    exact_peak(RodProblem{}, x, t);
    CHECK_NEAR(x, 0.42, 1e-12);
    CHECK_NEAR(t, 188.2, 1e-9);
}

TEST(study, peak_falls_back_to_the_hotter_end) {
    RodProblem p;
    p.heat_gen_W_m3 = 0.0;  // linear profile: hottest at x = 0
    double x = 1, t = 0;
    exact_peak(p, x, t);
    CHECK(x == 0.0 && t == 100.0);
    p.heat_gen_W_m3 = -5000.0;  // a sink: still the hotter end
    exact_peak(p, x, t);
    CHECK(x == 0.0 && t == 100.0);
}

TEST(study, matches_the_single_mesh_solver) {
    const Study s = run_study(RodProblem{}, {10, 20, 40, 80}, true);
    CHECK(s.meshes.size() == 4);
    CHECK_NEAR(s.meshes[0].error.max_midpoint_error_C, 1.25, 1e-9);
    CHECK_NEAR(s.meshes[3].observed_order, 2.0, 1e-6);
    CHECK_NEAR(s.meshes[2].theory_midpoint_error_C, 0.078125, 1e-12);
}

TEST(study, pointwise_error_peaks_inside_elements_and_vanishes_at_nodes) {
    const Study s = run_study(RodProblem{}, {10}, false);
    const auto& m = s.meshes[0];
    const double peak = *std::max_element(m.err_abs_C.begin(), m.err_abs_C.end());
    CHECK_NEAR(peak, 1.25, 1e-6);  // the arch top is the element midpoint
    // x = 0.3 is a node of the N = 10 mesh: sample index 0.3 * samples
    const std::size_t at_node = static_cast<std::size_t>(0.3 * (m.err_x_m.size() - 1) + 0.5);
    CHECK(m.err_abs_C[at_node] < 1e-9);
}

TEST(study, csv_exports_have_one_row_per_node_and_element) {
    const Study s = run_study(RodProblem{}, {10, 20}, true);
    CHECK(count_lines(nodes_csv(s)) == 1 + 11 + 21);
    CHECK(count_lines(midpoints_csv(s)) == 1 + 10 + 20);
    CHECK(count_lines(convergence_csv(s)) == 1 + 2);
}

TEST(study, limits_and_bad_input_are_reported) {
    CHECK(validate_study(RodProblem{}, {}, 100).find("at least one") != std::string::npos);
    CHECK(validate_study(RodProblem{}, {1000, 1000}, 1500).find("2002 nodes") != std::string::npos);
    RodProblem bad;
    bad.conductivity_W_mK = -1;
    CHECK(validate_study(bad, {10}, 100).find("conductivity") != std::string::npos);
}

TEST(study, cancelling_between_meshes_stops_the_study) {
    int calls = 0;
    const Study s = run_study(RodProblem{}, {10, 20, 40}, false, [&](double, const std::string&) { return ++calls < 2; });
    CHECK(s.cancelled);
    CHECK(s.meshes.size() == 1);
}
