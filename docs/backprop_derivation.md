# backprop derivation: study sheet for the handwritten appendix

> the brief asks for **your own handwritten** derivation (scans/photos, max two pages) and says you must be
> able to defend it. this sheet is a reference to learn from and check against. write it out yourself,
> in your own words, then compare your numbers with the c++ output in `results/backprop_gradcheck.csv`.
> do not submit this file as the handwritten appendix.

## setup

one sample x = 1, target y = 2. model and loss:

    z = w x + b
    h = tanh(z)
    ŷ = v h + c
    J = ½ (ŷ − y)²

start: w = 0.5, b = 0, v = 1, c = 0, learning rate η = 0.1.

## 1. forward pass

    z = 0.5 · 1 + 0          = 0.5
    h = tanh(0.5)            = 0.462117157   (tanh 0.5 = (e − 1)/(e + 1) with e = e^1 = 2.718281828 → 1.718281828/3.718281828)
    ŷ = 1 · 0.462117157 + 0  = 0.462117157
    e = ŷ − y                = −1.537882843
    J = ½ e²                 = ½ · 2.365083638 = 1.182541819

## 2. chain rule, symbol by symbol

work backwards from J. each line multiplies the previous result by one local derivative.

    ∂J/∂ŷ = ŷ − y = e                                   (from J = ½ e²)
    ∂ŷ/∂c = 1          →  ∂J/∂c = e
    ∂ŷ/∂v = h          →  ∂J/∂v = e · h
    ∂ŷ/∂h = v          →  ∂J/∂h = e · v
    ∂h/∂z = 1 − tanh²(z) = 1 − h²                       (derivative of tanh)
                       →  ∂J/∂z = e · v · (1 − h²)
    ∂z/∂w = x          →  ∂J/∂w = e · v · (1 − h²) · x
    ∂z/∂b = 1          →  ∂J/∂b = e · v · (1 − h²)

## 3. numbers

    h² = 0.462117157² = 0.213552267
    1 − h² = 0.786447733

    ∂J/∂c = e                              = −1.537882843
    ∂J/∂v = e · h = −1.537882843 · 0.462117157  = −0.710682047
    ∂J/∂z = e · v · (1 − h²) = −1.537882843 · 1 · 0.786447733 = −1.209464475
    ∂J/∂w = ∂J/∂z · x = −1.209464475 · 1   = −1.209464475
    ∂J/∂b = ∂J/∂z                           = −1.209464475

## 4. one simultaneous update

all four use the gradients computed from the **original** parameters (no parameter is updated before the
others' gradients are taken). θ_new = θ − η ∂J/∂θ:

    w = 0.5 − 0.1 · (−1.209464475) = 0.620946448
    b = 0   − 0.1 · (−1.209464475) = 0.120946448
    v = 1   − 0.1 · (−0.710682047) = 1.071068205
    c = 0   − 0.1 · (−1.537882843) = 0.153788284

check: with the new parameters z = 0.741892895, h = 0.630287, ŷ = 0.828869, J = 0.685774 (down from 1.182542).

## 5. direction of the gradients (the "explain" part)

- every gradient is **negative** because the prediction ŷ = 0.462 is **below** the target 2 (e < 0) and
  every parameter currently pushes ŷ upwards when increased (v > 0, h > 0, 1 − h² > 0, x > 0).
- gradient descent moves **against** the gradient, so all four parameters **increase**. that raises ŷ
  towards 2 and lowers J, which the check above confirms.
- c gets the largest step because ∂ŷ/∂c = 1; w and b get smaller steps because the signal passes through
  tanh, whose slope 1 − h² = 0.786 is below 1; v's step is scaled by h = 0.462.
- w and b have identical gradients here only because x = 1.

## 6. how the c++ check works (task f, second half)

`src/core/backprop.cpp` codes the same formulas by hand (no autodiff). `finite_difference_gradients` nudges
one parameter at a time by ±ε (ε = 1e-6) and uses (J(θ+ε) − J(θ−ε)) / 2ε. every analytic gradient agrees
to ≤ 1.2e-10 (tolerance 1e-5). a deliberate wrong derivative (using 1 − h instead of 1 − h²) fails this check,
see `tests/test_backprop.cpp`.

the loss over 100 further steps (`results/backprop_loss.svg`) falls monotonically, close to a straight line on
a log axis, to about 1e-20 after 101 steps. the model can fit one point exactly (c alone could absorb the
error), so the loss heads to zero. the descent is roughly geometric because near the optimum the loss is
locally quadratic and each step shrinks the error by about the same factor.
