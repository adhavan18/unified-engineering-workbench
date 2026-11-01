#pragma once
// a mesh-convergence study: solve the rod on several meshes and measure each
// against the exact solution. shared by the live (synchronous) api route, the
// background job and the report tool, so they can never disagree.

#include <functional>
#include <string>
#include <vector>

#include "core/fea1d.hpp"

namespace wb::fea {

struct MeshResult {
    int elements = 0;
    ErrorReport error;
    double theory_midpoint_error_C = 0.0;  // q h^2 / (8 k)
    double observed_order = 0.0;           // vs the previous mesh; 0 for the first
    std::vector<double> plot_x_m, plot_T_C;  // decimated to <= ~2000 points
    std::vector<double> err_x_m, err_abs_C;  // |T_fe(x) - T_exact(x)| sampled along the rod
    Solution solution;                       // full nodal solution (kept only on request)
};

struct Study {
    RodProblem rod;
    std::vector<MeshResult> meshes;
    std::vector<double> exact_x_m, exact_T_C;
    double peak_x_m = 0.0, peak_T_C = 0.0;  // of the exact solution
    bool cancelled = false;
};

// returns "" or a reason the study cannot run (bad inputs, too many nodes).
std::string validate_study(const RodProblem& rod, const std::vector<int>& meshes, long long max_total_nodes);

// progress(fraction, message) may return false to cancel between meshes.
Study run_study(const RodProblem& rod, const std::vector<int>& meshes, bool keep_solutions,
                const std::function<bool(double, const std::string&)>& progress = {});

// csv exports (need keep_solutions = true for nodes/midpoints)
std::string nodes_csv(const Study& s);
std::string midpoints_csv(const Study& s);
std::string convergence_csv(const Study& s);

// location and value of the maximum of the exact solution on [0, L]
void exact_peak(const RodProblem& p, double& x_m, double& T_C);

}  // namespace wb::fea
