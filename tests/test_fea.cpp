#include <cmath>
#include <stdexcept>

#include "core/fea1d.hpp"
#include "test_framework.hpp"

using namespace wb::fea;

namespace {
const RodProblem kBrief{};  // defaults are the task c values
}

TEST(fea, defaults_match_the_brief) {
    CHECK(kBrief.length_m == 1.0);
    CHECK(kBrief.area_m2 == 1e-4);
    CHECK(kBrief.conductivity_W_mK == 10.0);
    CHECK(kBrief.heat_gen_W_m3 == 10000.0);
    CHECK(kBrief.t_left_C == 100.0);
    CHECK(kBrief.t_right_C == 20.0);
}

TEST(fea, exact_solution_matches_brief_formula) {
    for (double x : {0.0, 0.1, 0.42, 0.5, 0.9, 1.0}) {
        const double brief = 100.0 - 80.0 * x + (10000.0 / (2.0 * 10.0)) * x * (1.0 - x);
        CHECK_NEAR(exact_temperature(kBrief, x), brief, 1e-12);
    }
}

TEST(fea, assembly_rows_sum_to_zero_and_load_sums_to_qAL) {
    const LinearSystem s = assemble(kBrief, 10);
    // a uniform temperature must produce zero conducted heat in every row
    for (std::size_t i = 0; i < s.diag.size(); ++i) {
        double row = s.diag[i];
        if (i > 0) row += s.off[i - 1];
        if (i < s.off.size()) row += s.off[i];
        CHECK_NEAR(row, 0.0, 1e-12);
    }
    double total = 0.0;
    for (double f : s.rhs) total += f;
    CHECK_NEAR(total, 10000.0 * 1e-4 * 1.0, 1e-12);  // q A L = 1 W
    CHECK_NEAR(s.diag[5], 2.0 * 10.0 * 1e-4 / 0.1, 1e-12);  // interior: 2 kA/h
    CHECK_NEAR(s.rhs[0], 10000.0 * 1e-4 * 0.1 / 2.0, 1e-15);  // end node: half an element
}

TEST(fea, boundary_temperatures_are_imposed_exactly) {
    for (int n : {1, 2, 10, 80}) {
        const Solution s = solve(kBrief, n);
        CHECK(s.temp_C.front() == 100.0);
        CHECK(s.temp_C.back() == 20.0);
        CHECK(s.x_m.back() == 1.0);
    }
}

TEST(fea, nodal_values_are_exact_for_linear_elements_in_1d) {
    // known 1d property: with exact load integration the fe nodal values equal
    // the exact solution, so all discretisation error lives between nodes.
    for (int n : {10, 20, 40, 80}) {
        const ErrorReport r = measure_error(kBrief, solve(kBrief, n));
        CHECK(r.max_nodal_error_C < 1e-9);
    }
}

TEST(fea, midpoint_error_matches_interpolation_theory) {
    // T is quadratic, so the linear interpolant's midpoint error is h^2/8 * |T''| = q h^2 / (8 k)
    const double expected[] = {1.25, 0.3125, 0.078125, 0.01953125};
    const int meshes[] = {10, 20, 40, 80};
    for (int i = 0; i < 4; ++i) {
        const ErrorReport r = measure_error(kBrief, solve(kBrief, meshes[i]));
        CHECK_NEAR(r.max_midpoint_error_C, expected[i], 1e-9);
    }
}

TEST(fea, observed_order_is_two) {
    ErrorReport prev = measure_error(kBrief, solve(kBrief, 10));
    for (int n : {20, 40, 80, 160}) {
        const ErrorReport cur = measure_error(kBrief, solve(kBrief, n));
        CHECK_NEAR(observed_order(prev, cur), 2.0, 1e-6);
        prev = cur;
    }
}

TEST(fea, zero_source_gives_linear_profile_everywhere) {
    RodProblem p = kBrief;
    p.heat_gen_W_m3 = 0.0;
    const Solution s = solve(p, 7);
    for (double x = 0.0; x <= 1.0; x += 0.03) CHECK_NEAR(interpolate(s, x), 100.0 - 80.0 * x, 1e-10);
}

TEST(fea, interpolation_hits_nodes_and_midpoints) {
    const Solution s = solve(kBrief, 4);
    CHECK_NEAR(interpolate(s, 0.25), s.temp_C[1], 1e-12);
    CHECK_NEAR(interpolate(s, 0.125), 0.5 * (s.temp_C[0] + s.temp_C[1]), 1e-12);
    CHECK_NEAR(interpolate(s, 1.0), 20.0, 1e-12);
}

TEST(fea, peak_temperature_location) {
    // dT/dx = 0 at x = 0.42 m for the brief's data, T = 188.2 C
    const Solution s = solve(kBrief, 100);
    CHECK_NEAR(s.x_m[42], 0.42, 1e-12);
    CHECK_NEAR(s.temp_C[42], 188.2, 1e-9);
}

TEST(fea, invalid_inputs_are_rejected_with_reasons) {
    RodProblem p = kBrief;
    p.conductivity_W_mK = 0.0;
    CHECK_THROWS_WITH(solve(p, 10), std::invalid_argument, "conductivity");
    p = kBrief;
    p.area_m2 = -1.0;
    CHECK_THROWS_WITH(solve(p, 10), std::invalid_argument, "area");
    p = kBrief;
    p.length_m = NAN;
    CHECK_THROWS_WITH(solve(p, 10), std::invalid_argument, "length");
    p = kBrief;
    p.t_left_C = -300.0;
    CHECK_THROWS_WITH(solve(p, 10), std::invalid_argument, "absolute zero");
    CHECK_THROWS_WITH(solve(kBrief, 0), std::invalid_argument, "at least 1");
    CHECK_THROWS_WITH(solve(kBrief, kMaxElements + 1), std::invalid_argument, "limited");
}
