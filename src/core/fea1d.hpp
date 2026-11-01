#pragma once
// steady-state 1d thermal fe solver for a uniform rod with two-node linear elements.
//
//   -d/dx( k A dT/dx ) = q A      on 0 < x < L
//   T(0) = t_left,  T(L) = t_right
//
// all quantities are si except temperatures, which are in degrees celsius
// (only temperature differences enter the conduction equation, so C vs K is safe).

#include <string>
#include <vector>

namespace wb::fea {

constexpr double kAbsoluteZero_C = -273.15;
constexpr int kMaxElements = 5'000'000;

struct RodProblem {
    double length_m = 1.0;
    double area_m2 = 1e-4;
    double conductivity_W_mK = 10.0;
    double heat_gen_W_m3 = 10'000.0;
    double t_left_C = 100.0;
    double t_right_C = 20.0;
};

// global system after assembly; `off[i]` couples nodes i and i+1.
struct LinearSystem {
    std::vector<double> diag;  // W/K
    std::vector<double> off;   // W/K
    std::vector<double> rhs;   // W
};

struct Solution {
    std::vector<double> x_m;
    std::vector<double> temp_C;
};

struct ErrorReport {
    int elements = 0;
    double h_m = 0.0;
    double max_nodal_error_C = 0.0;
    double max_midpoint_error_C = 0.0;
    double worst_midpoint_x_m = 0.0;
};

// empty string when the inputs are usable, otherwise a reason a person can act on.
std::string validate(const RodProblem& p, int elements);

// element stiffness kA/h and consistent load qAh/2 summed into the global system.
LinearSystem assemble(const RodProblem& p, int elements);

// imposes T(node) = value while keeping the matrix symmetric: the known column is
// moved to the right-hand side, then the row is replaced by the identity.
void apply_dirichlet(LinearSystem& sys, int node, double value_C);

// throws std::invalid_argument with the validate() message on bad input.
Solution solve(const RodProblem& p, int elements);

double exact_temperature(const RodProblem& p, double x_m);

// linear fe interpolation of a uniform-mesh solution at any x in [0, L].
double interpolate(const Solution& s, double x_m);

ErrorReport measure_error(const RodProblem& p, const Solution& s);

// p in  e ~ C h^p, from two meshes.
double observed_order(const ErrorReport& coarse, const ErrorReport& fine);

}  // namespace wb::fea
