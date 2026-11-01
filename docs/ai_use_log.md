# ai-use log

the brief allows any ai assistant if its use is disclosed. this log records how ai was used, what it
produced, how it was checked, and where it was wrong. nothing here is invented: prompts are quoted from the
session, and every "wrong" item below actually happened and is visible in the git history or test output.

## tools

| tool | model | used for |
|---|---|---|
| claude code (anthropic), desktop app, agent mode | claude opus 5.5 (`claude-opus-5-5`) | design, c++ and frontend code, tests, docs, running builds/tests/benchmarks, browser click-through of the ui |
| python 3 (local) | – | independent reference values for the backprop tests and the study sheet |
| microsoft edge (headless) | – | rendering svg plots to png for visual checks, report pdf |

session: 2026-11-01, roughly 09:55–11:00 local time. all commits in `repo/` from that window were made
during this session and carry a `Co-Authored-By: Claude` trailer.

## representative prompts (verbatim, from the candidate)

1. *the full assignment brief pasted as text, followed by:* "finish this"
2. *the brief attached again as the original pdf.*
3. answer to "ok to install a c++ compiler?": "Yes, install WinLibs (Recommended)"
4. on the candidate id: "idk my candidateid where do i get it? is it my register number college roll no?"
5. "do it for real man. i need alll smallcase and good submissions."
6. "what is the task"

the prompts were short; the brief itself served as the specification. the assistant was asked (by its own
operating rules and by prompt 5) to work from the brief, keep the docs lowercase and not fabricate evidence.

## what the assistant did, and how it was validated

| area | ai output | independent validation |
|---|---|---|
| fea solver | assembly, dirichlet handling, thomas solver, error measures | tests against closed-form facts: row sums 0, total load qAL, exact nodal values, midpoint error = qh²/8k to 1e-9, observed order 2 ± 1e-6 |
| batch / threads | worker pool, case generator, benchmark harness | tests: identical output for 1/2/3/4/7 workers, input order kept, cancellation, pool reuse after an exception; 3 benchmark sessions; two timing probes (below) |
| parsers | stl and csv readers | 20+ negative tests with hand-written malformed fixtures; 1M-node and 1.2M-triangle files through the real ui |
| backprop | hand-derived gradients in c++ | reference values computed separately in python; central differences; a deliberately wrong derivative is shown to fail the check |
| ui | html/css/js, webgl viewer, plots | clicked through every view in a browser; node picking checked against the generating formula (node 1125 → 179.75 °C = T_exact(0.55)) |
| packaging | static link, launcher | `objdump -p` shows only windows system dlls; launch-to-ready timed 5× |

## where the ai was wrong or unsuitable (all caught, all fixed)

1. **benchmark design was biased.** the first harness timed each configuration as a block (serial ×6, then
   p=1 ×6, …). result: the 1-worker pool looked ~1.5× *slower* than serial in every run. the assistant's
   first explanation, "windows puts the pool thread on an efficiency core", was **wrong**:
   `tools/experiments/thread_probe.cpp` showed a fresh thread runs as fast as the main thread.
   `order_probe.cpp`, alternating serial and pool(1), gave a ratio of ~1.0, and both slowed from ~0.40 s to
   ~0.60 s after a few seconds: the laptop's turbo budget runs out, so later blocks look slower. fix:
   interleave configurations inside each repetition (commit `b603749`).
2. **a test with a guessed expectation.** the stl example test asserted "more than 100 triangles"; the heat
   sink has 8 boxes × 12 = 96. the test was wrong, not the parser; it now asserts exactly 96.
3. **an off-by-one in the csv reader.** a trailing newline was counted as an extra blank line
   (`while (pos <= size)`). found by a unit test, fixed to `pos < size`.
4. **a mislabelled size-test file.** the "over limit" stl was 250 MB / 1.92M triangles, under both limits,
   and parsed fine. regenerated at 2.16M triangles / 282 MB, which is now correctly rejected.
5. **wrong digits in the hand-typed backprop sheet.** h², 1 − h², e² and the post-update h and ŷ were
   off in the 4th–9th digit. an independent python check caught it before commit.
6. **an unverified claim in the readme.** it said msvc builds "also work"; nothing had been built with msvc.
   the sentence now says only gcc was built and tested.
7. **ui bugs found by clicking, not by tests:** `[hidden]` overridden by css `display` rules (empty-state
   text stayed over the 3d view); the file name printed twice in parse errors; "launch → ui ready" computed
   from process start even after a page reload (showed 90 s); the job label stuck on "queued" for the first
   benchmark run.
8. **a misread test result.** a scripted click on the point cloud returned nothing; the cause was the test
   harness (synthetic pointer ids cannot be captured), not the picker. a real click worked. the viewer was
   still hardened to ignore capture failures.

## challenge test

`tests/test_backprop.cpp :: gradient_check_catches_a_wrong_derivative` deliberately uses the common slip
tanh' = 1 − h (instead of 1 − h²) and asserts that the finite-difference check rejects it (difference
> 1e-2). it does.

## candidate's own edits and review

*(to be completed by the candidate: list what you changed yourself after the ai session, what you
re-derived by hand, and which parts you can explain without notes. do not leave this empty.)*
