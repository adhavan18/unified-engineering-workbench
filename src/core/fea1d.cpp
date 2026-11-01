#include "core/fea1d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/tridiagonal.hpp"

namespace wb::fea {

namespace {

bool positive_finite(double v) { return std::isfinite(v) && v > 0.0; }

}  // namespace

std::string validate(const RodProblem& p, int elements) {
    if (!positive_finite(p.length_m)) return "length L must be a positive number of metres";
    if (!positive_finite(p.area_m2)) return "area A must be a positive number of m^2";
    if (!positive_finite(p.conductivity_W_mK)) return "conductivity k must be positive, in W/(m K)";
    if (!std::isfinite(p.heat_gen_W_m3)) return "heat generation q must be a finite number of W/m^3";
    if (!std::isfinite(p.t_left_C) || !std::isfinite(p.t_right_C)) {
        return "boundary temperatures T(0) and T(L) must be finite, in C";
    }
    if (p.t_left_C < kAbsoluteZero_C || p.t_right_C < kAbsoluteZero_C) {
        return "boundary temperatures cannot be below absolute zero (-273.15 C)";
    }
    if (elements < 1) return "the mesh needs at least 1 element";
    if (elements > kMaxElements) return "the mesh is limited to 5,000,000 elements";
    return {};
}

LinearSystem assemble(const RodProblem& p, int elements) {
    const std::size_t nodes = static_cast<std::size_t>(elements) + 1;
    const double h = p.length_m / elements;
    const double ke = p.conductivity_W_mK * p.area_m2 / h;  // W/K
    const double fe = p.heat_gen_W_m3 * p.area_m2 * h / 2.0;  // W, half to each node

    LinearSystem sys;
    sys.diag.assign(nodes, 0.0);
    sys.off.assign(nodes - 1, 0.0);
    sys.rhs.assign(nodes, 0.0);
    for (std::size_t e = 0; e + 1 < nodes; ++e) {
        // element matrix ke * [[1, -1], [-1, 1]] and load fe * [1, 1]
        sys.diag[e] += ke;
        sys.diag[e + 1] += ke;
        sys.off[e] -= ke;
        sys.rhs[e] += fe;
        sys.rhs[e + 1] += fe;
    }
    return sys;
}

void apply_dirichlet(LinearSystem& sys, int node, double value_C) {
    const std::size_t n = sys.diag.size();
    const std::size_t i = static_cast<std::size_t>(node);
    if (i >= n) throw std::out_of_range("dirichlet node outside the mesh");
    if (i > 0) {
        sys.rhs[i - 1] -= sys.off[i - 1] * value_C;
        sys.off[i - 1] = 0.0;
    }
    if (i + 1 < n) {
        sys.rhs[i + 1] -= sys.off[i] * value_C;
        sys.off[i] = 0.0;
    }
    sys.diag[i] = 1.0;
    sys.rhs[i] = value_C;
}

Solution solve(const RodProblem& p, int elements) {
    if (auto why = validate(p, elements); !why.empty()) throw std::invalid_argument(why);

    LinearSystem sys = assemble(p, elements);
    apply_dirichlet(sys, 0, p.t_left_C);
    apply_dirichlet(sys, elements, p.t_right_C);

    Solution s;
    s.temp_C = solve_tridiagonal(std::move(sys.diag), sys.off, std::move(sys.rhs));
    s.x_m.resize(s.temp_C.size());
    for (std::size_t i = 0; i < s.x_m.size(); ++i) {
        // i * L / N rather than accumulating h keeps the last node exactly at L
        s.x_m[i] = p.length_m * static_cast<double>(i) / elements;
    }
    return s;
}

double exact_temperature(const RodProblem& p, double x_m) {
    const double L = p.length_m;
    return p.t_left_C + (p.t_right_C - p.t_left_C) * x_m / L +
           p.heat_gen_W_m3 / (2.0 * p.conductivity_W_mK) * x_m * (L - x_m);
}

double interpolate(const Solution& s, double x_m) {
    const std::size_t elements = s.x_m.size() - 1;
    const double L = s.x_m.back();
    const double xc = std::clamp(x_m, 0.0, L);
    std::size_t e = static_cast<std::size_t>(xc / L * static_cast<double>(elements));
    e = std::min(e, elements - 1);
    const double x0 = s.x_m[e];
    const double x1 = s.x_m[e + 1];
    const double xi = (xc - x0) / (x1 - x0);  // local coordinate in [0, 1]
    return (1.0 - xi) * s.temp_C[e] + xi * s.temp_C[e + 1];
}

ErrorReport measure_error(const RodProblem& p, const Solution& s) {
    ErrorReport r;
    r.elements = static_cast<int>(s.x_m.size()) - 1;
    r.h_m = p.length_m / r.elements;
    for (std::size_t i = 0; i < s.x_m.size(); ++i) {
        const double err = std::abs(s.temp_C[i] - exact_temperature(p, s.x_m[i]));
        r.max_nodal_error_C = std::max(r.max_nodal_error_C, err);
    }
    for (std::size_t e = 0; e + 1 < s.x_m.size(); ++e) {
        const double xm = 0.5 * (s.x_m[e] + s.x_m[e + 1]);
        const double fe_value = 0.5 * (s.temp_C[e] + s.temp_C[e + 1]);  // fe interpolation
        const double err = std::abs(fe_value - exact_temperature(p, xm));
        if (err > r.max_midpoint_error_C) {
            r.max_midpoint_error_C = err;
            r.worst_midpoint_x_m = xm;
        }
    }
    return r;
}

double observed_order(const ErrorReport& coarse, const ErrorReport& fine) {
    if (coarse.max_midpoint_error_C <= 0.0 || fine.max_midpoint_error_C <= 0.0) return 0.0;
    return std::log(coarse.max_midpoint_error_C / fine.max_midpoint_error_C) /
           std::log(coarse.h_m / fine.h_m);
}

}  // namespace wb::fea
