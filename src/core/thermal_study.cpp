#include "core/thermal_study.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace wb::fea {

namespace {

constexpr int kExactSamples = 400;
constexpr int kErrorSamples = 800;

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

}  // namespace

void exact_peak(const RodProblem& p, double& x_m, double& T_C) {
    // T'(x) = (TL - T0)/L + q/(2k) (L - 2x) = 0
    const double L = p.length_m;
    x_m = 0.0;
    T_C = p.t_left_C;
    if (p.t_right_C > T_C) { x_m = L; T_C = p.t_right_C; }
    if (p.heat_gen_W_m3 > 0.0) {
        const double xs = L / 2.0 + p.conductivity_W_mK * (p.t_right_C - p.t_left_C) / (p.heat_gen_W_m3 * L);
        if (xs > 0.0 && xs < L && exact_temperature(p, xs) > T_C) {
            x_m = xs;
            T_C = exact_temperature(p, xs);
        }
    }
}

std::string validate_study(const RodProblem& rod, const std::vector<int>& meshes, long long max_total_nodes) {
    if (meshes.empty()) return "choose at least one mesh";
    long long total = 0;
    for (int n : meshes) {
        if (auto why = validate(rod, n); !why.empty()) return why;
        total += n + 1;
    }
    if (total > max_total_nodes) {
        return "these meshes have " + std::to_string(total) + " nodes in total; the limit here is " +
               std::to_string(max_total_nodes);
    }
    return {};
}

Study run_study(const RodProblem& rod, const std::vector<int>& meshes, bool keep_solutions,
                const std::function<bool(double, const std::string&)>& progress) {
    Study st;
    st.rod = rod;
    for (int i = 0; i <= kExactSamples; ++i) {
        const double x = rod.length_m * i / kExactSamples;
        st.exact_x_m.push_back(x);
        st.exact_T_C.push_back(exact_temperature(rod, x));
    }
    exact_peak(rod, st.peak_x_m, st.peak_T_C);

    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const int n = meshes[m];
        if (progress && !progress(static_cast<double>(m) / meshes.size(), "solving N = " + std::to_string(n))) {
            st.cancelled = true;
            return st;
        }
        MeshResult r;
        r.elements = n;
        Solution s = solve(rod, n);
        r.error = measure_error(rod, s);
        r.theory_midpoint_error_C = rod.heat_gen_W_m3 * r.error.h_m * r.error.h_m / (8.0 * rod.conductivity_W_mK);
        if (!st.meshes.empty()) r.observed_order = observed_order(st.meshes.back().error, r.error);

        const std::size_t stride = std::max<std::size_t>(1, s.x_m.size() / 2000);
        for (std::size_t i = 0; i < s.x_m.size(); i += stride) {
            r.plot_x_m.push_back(s.x_m[i]);
            r.plot_T_C.push_back(s.temp_C[i]);
        }
        if (r.plot_x_m.back() != s.x_m.back()) {
            r.plot_x_m.push_back(s.x_m.back());
            r.plot_T_C.push_back(s.temp_C.back());
        }
        // sample the interpolation error finely enough to show the arch inside each element
        const int samples = std::max(kErrorSamples, std::min(8 * n, 4000));
        for (int i = 0; i <= samples; ++i) {
            const double x = rod.length_m * i / samples;
            r.err_x_m.push_back(x);
            r.err_abs_C.push_back(std::abs(interpolate(s, x) - exact_temperature(rod, x)));
        }
        if (keep_solutions) r.solution = std::move(s);
        st.meshes.push_back(std::move(r));
    }
    return st;
}

std::string nodes_csv(const Study& s) {
    std::ostringstream o;
    o << "elements,node,x_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (const auto& m : s.meshes) {
        const auto& sol = m.solution;
        for (std::size_t i = 0; i < sol.x_m.size(); ++i) {
            const double te = exact_temperature(s.rod, sol.x_m[i]);
            o << m.elements << ',' << i << ',' << fmt(sol.x_m[i]) << ',' << fmt(sol.temp_C[i]) << ',' << fmt(te) << ','
              << fmt(std::abs(sol.temp_C[i] - te)) << '\n';
        }
    }
    return o.str();
}

std::string midpoints_csv(const Study& s) {
    std::ostringstream o;
    o << "elements,element,x_mid_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (const auto& m : s.meshes) {
        const auto& sol = m.solution;
        for (std::size_t e = 0; e + 1 < sol.x_m.size(); ++e) {
            const double xm = 0.5 * (sol.x_m[e] + sol.x_m[e + 1]);
            const double tf = 0.5 * (sol.temp_C[e] + sol.temp_C[e + 1]);
            const double te = exact_temperature(s.rod, xm);
            o << m.elements << ',' << e << ',' << fmt(xm) << ',' << fmt(tf) << ',' << fmt(te) << ','
              << fmt(std::abs(tf - te)) << '\n';
        }
    }
    return o.str();
}

std::string convergence_csv(const Study& s) {
    std::ostringstream o;
    o << "elements,h_m,max_nodal_error_C,max_midpoint_error_C,theory_midpoint_error_C,observed_order\n";
    for (std::size_t m = 0; m < s.meshes.size(); ++m) {
        const auto& r = s.meshes[m];
        o << r.elements << ',' << fmt(r.error.h_m) << ',' << fmt(r.error.max_nodal_error_C) << ','
          << fmt(r.error.max_midpoint_error_C) << ',' << fmt(r.theory_midpoint_error_C) << ','
          << (m ? fmt(r.observed_order) : "") << '\n';
    }
    return o.str();
}

}  // namespace wb::fea
