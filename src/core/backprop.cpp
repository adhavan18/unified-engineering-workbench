#include "core/backprop.hpp"

#include <cmath>

namespace wb::nn {

Forward forward(const Params& p, double x, double y) {
    Forward f;
    f.z = p.w * x + p.b;
    f.h = std::tanh(f.z);
    f.y_hat = p.v * f.h + p.c;
    const double e = f.y_hat - y;
    f.loss = 0.5 * e * e;
    return f;
}

Gradients analytic_gradients(const Params& p, double x, double y) {
    const Forward f = forward(p, x, y);
    const double dJ_dyhat = f.y_hat - y;            // from J = 0.5 e^2
    const double dJ_dh = dJ_dyhat * p.v;            // y_hat = v h + c
    const double dJ_dz = dJ_dh * (1.0 - f.h * f.h);  // d tanh(z)/dz = 1 - tanh^2
    return {dJ_dz * x, dJ_dz, dJ_dyhat * f.h, dJ_dyhat};
}

Gradients finite_difference_gradients(const Params& p, double x, double y, double eps) {
    auto loss_with = [&](double Params::*field, double delta) {
        Params q = p;
        q.*field += delta;
        return forward(q, x, y).loss;
    };
    auto central = [&](double Params::*field) {
        return (loss_with(field, eps) - loss_with(field, -eps)) / (2.0 * eps);
    };
    return {central(&Params::w), central(&Params::b), central(&Params::v), central(&Params::c)};
}

Params sgd_step(const Params& p, const Gradients& g, double learning_rate) {
    return {p.w - learning_rate * g.dw, p.b - learning_rate * g.db, p.v - learning_rate * g.dv,
            p.c - learning_rate * g.dc};
}

std::vector<double> train_losses(Params p, double x, double y, double learning_rate, int steps) {
    std::vector<double> losses;
    losses.reserve(static_cast<std::size_t>(steps) + 1);
    losses.push_back(forward(p, x, y).loss);
    for (int s = 0; s < steps; ++s) {
        p = sgd_step(p, analytic_gradients(p, x, y), learning_rate);
        losses.push_back(forward(p, x, y).loss);
    }
    return losses;
}

}  // namespace wb::nn
