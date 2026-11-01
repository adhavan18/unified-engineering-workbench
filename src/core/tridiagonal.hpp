#pragma once
// thomas algorithm for symmetric tridiagonal systems.

#include <stdexcept>
#include <vector>

namespace wb {

// solves A x = rhs where A has `diag` on the main diagonal and `off` on both
// the sub- and super-diagonal (off[i] couples rows i and i+1).
// no pivoting: fine for the diagonally dominant spd matrices that 1d fe
// assembly produces, and it throws instead of dividing by zero otherwise.
inline std::vector<double> solve_tridiagonal(std::vector<double> diag,
                                             const std::vector<double>& off,
                                             std::vector<double> rhs) {
    const std::size_t n = diag.size();
    if (n == 0 || rhs.size() != n || off.size() + 1 != n) {
        throw std::invalid_argument("tridiagonal system has inconsistent sizes");
    }
    for (std::size_t i = 1; i < n; ++i) {
        if (diag[i - 1] == 0.0) throw std::runtime_error("zero pivot in tridiagonal solve");
        const double m = off[i - 1] / diag[i - 1];
        diag[i] -= m * off[i - 1];
        rhs[i] -= m * rhs[i - 1];
    }
    if (diag[n - 1] == 0.0) throw std::runtime_error("zero pivot in tridiagonal solve");

    std::vector<double> x(n);
    x[n - 1] = rhs[n - 1] / diag[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) {
        x[i] = (rhs[i] - off[i] * x[i + 1]) / diag[i];
    }
    return x;
}

}  // namespace wb
