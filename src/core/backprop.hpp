#pragma once
// task f: one hidden tanh unit, hand-derived gradients, no autodiff.
//
//   z = w x + b,  h = tanh(z),  y_hat = v h + c,  J = 0.5 (y_hat - y)^2
//
//   dJ/dc = (y_hat - y)
//   dJ/dv = (y_hat - y) h
//   dJ/db = (y_hat - y) v (1 - h^2)
//   dJ/dw = (y_hat - y) v (1 - h^2) x

#include <vector>

namespace wb::nn {

struct Params {
    double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct Forward {
    double z, h, y_hat, loss;
};

struct Gradients {
    double dw, db, dv, dc;
};

Forward forward(const Params& p, double x, double y);
Gradients analytic_gradients(const Params& p, double x, double y);

// central differences (J(theta + eps) - J(theta - eps)) / (2 eps), one parameter at a time.
Gradients finite_difference_gradients(const Params& p, double x, double y, double eps);

// simultaneous update: all four gradients come from the same (old) parameters.
Params sgd_step(const Params& p, const Gradients& g, double learning_rate);

// loss before any update followed by the loss after each of `steps` updates.
std::vector<double> train_losses(Params p, double x, double y, double learning_rate, int steps);

}  // namespace wb::nn
