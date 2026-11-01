#include <cmath>

#include "core/backprop.hpp"
#include "test_framework.hpp"

using namespace wb::nn;

// reference values computed independently in python (math.tanh, double precision)
TEST(backprop, forward_pass_matches_reference) {
    const Forward f = forward(Params{}, 1.0, 2.0);
    CHECK_NEAR(f.z, 0.5, 0.0);
    CHECK_NEAR(f.h, 0.46211715726000974, 1e-15);
    CHECK_NEAR(f.y_hat, 0.46211715726000974, 1e-15);
    CHECK_NEAR(f.loss, 1.1825418189970167, 1e-14);
}

TEST(backprop, analytic_gradients_match_reference) {
    const Gradients g = analytic_gradients(Params{}, 1.0, 2.0);
    CHECK_NEAR(g.dw, -1.2094644752400612, 1e-14);
    CHECK_NEAR(g.db, -1.2094644752400612, 1e-14);
    CHECK_NEAR(g.dv, -0.7106820474859469, 1e-14);
    CHECK_NEAR(g.dc, -1.5378828427399902, 1e-14);
}

TEST(backprop, central_differences_agree_within_1e_5) {
    const Params p{};
    const Gradients a = analytic_gradients(p, 1.0, 2.0);
    const Gradients n = finite_difference_gradients(p, 1.0, 2.0, 1e-6);
    CHECK_NEAR(a.dw, n.dw, 1e-5);
    CHECK_NEAR(a.db, n.db, 1e-5);
    CHECK_NEAR(a.dv, n.dv, 1e-5);
    CHECK_NEAR(a.dc, n.dc, 1e-5);
}

TEST(backprop, gradient_check_catches_a_wrong_derivative) {
    // challenge test: if tanh' were taken as (1 - h) instead of (1 - h^2), the
    // check must fail. this guards against the most common hand-derivation slip.
    const Params p{};
    const Forward f = forward(p, 1.0, 2.0);
    const double wrong_dw = (f.y_hat - 2.0) * p.v * (1.0 - f.h) * 1.0;
    const Gradients n = finite_difference_gradients(p, 1.0, 2.0, 1e-6);
    CHECK(std::abs(wrong_dw - n.dw) > 1e-2);
}

TEST(backprop, one_simultaneous_update_matches_reference) {
    const Params p0{};
    const Params p1 = sgd_step(p0, analytic_gradients(p0, 1.0, 2.0), 0.1);
    CHECK_NEAR(p1.w, 0.6209464475240061, 1e-14);
    CHECK_NEAR(p1.b, 0.12094644752400613, 1e-14);
    CHECK_NEAR(p1.v, 1.0710682047485947, 1e-14);
    CHECK_NEAR(p1.c, 0.15378828427399904, 1e-14);
    CHECK_NEAR(forward(p1, 1.0, 2.0).loss, 0.6857737650448423, 1e-13);
}

TEST(backprop, loss_decreases_monotonically_over_training) {
    const auto losses = train_losses(Params{}, 1.0, 2.0, 0.1, 101);
    CHECK(losses.size() == 102);
    for (std::size_t i = 1; i < losses.size(); ++i) CHECK(losses[i] < losses[i - 1]);
    CHECK(losses.back() < 1e-3);
}
