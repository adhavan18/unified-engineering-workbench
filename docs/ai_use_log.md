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

sessions: 2026-11-01, roughly 09:55–11:05 (first build), 11:20–12:10 (ui rebuild) and 13:40–14:05 (audit
against the brief), and 2026-11-02 05:10–05:45 (final verification), local time; see [effort_log.md](effort_log.md). every commit from these windows was made
during the sessions and carries a `Co-Authored-By: Claude` trailer.

## representative prompts (verbatim, from the candidate)

1. *the full assignment brief pasted as text, followed by:* "finish this"
2. *the brief attached again as the original pdf.*
3. answer to "ok to install a c++ compiler?": "Yes, install WinLibs (Recommended)"
4. on the candidate id: "idk my candidateid where do i get it? is it my register number college roll no?"
5. "do it for real man. i need alll smallcase and good submissions."
6. "what is the task"
7. after seeing the first version: "the ui is shit. idk if it fits thhe entir erquirements it so stupdi its not
   interactive. idk its not looking submission ready"
8. a pasted group-chat thread from other candidates: the google form only accepts one pdf of at most 1 mb,
   while section 8 asks for a zip.
9. "is everything done? show me"
10. "make this the best submission. fine tune this however u can and follow the doc." and, mid-way,
    "are u following it?"
11. "are u done with everything. can u pakc it and zip?. check everythingn i need everything tgo be proper.
    put maximum effort."

prompt 7 drove the second iteration: the assistant listed what was actually weak (form → button → static
picture everywhere, an almost empty 3d example, nothing showing how the brief maps to the app) and rebuilt
the ui around live interaction plus an in-app requirement checklist. prompt 8 led to a ≤ 1 mb submission pdf
with a link to the zip and its sha-256, while waiting for the organisers' answer.

prompt 10 drove a third pass: the assistant re-read the brief and the rubric line by line and checked each
against the repo and the running app. what it found and fixed is items 16–19 below. prompt 11 led to a
final verification from a fresh clone of the bundle (items 20–21).

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
   interleave configurations inside each repetition (commit `91ef90f`).
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

9. **files left out of version control.** the report tool (`tools/report_main.cpp`, `src/report/`) generated
   every file in `results/` but was never `git add`-ed, so the committed CMakeLists referred to files the repo
   did not contain. found by checking `git status` before packaging; committed, and a fresh clone was built
   and tested to confirm the repo is complete.
10. **an absolute path in a saved log.** the launcher logged the frontend folder as a full machine path, which
   the brief forbids in the submission. the launcher now logs it relative to the exe; the one line in the
   saved walkthrough log was made relative and marked as such.

11. **the first ui was functionally complete but not good enough.** every view was a form plus a static
   result, and the 3d example was a 1 m × 10 mm rod, almost a line. the candidate called this out (prompt 7);
   the second iteration is the one submitted. the first ui is kept in the git history.
12. **a duplicate `const` broke the whole frontend.** in the rebuilt geometry view a second `const size`
   in one function made the module fail to load, so every page went blank. `node --check` did not flag it;
   the browser console did. headless screenshots of blank pages were the first symptom.
13. **a log-scale plot that hid the point.** the pointwise error is exactly zero at every node, so on a log
   axis the arches became a wall of spikes down to the clamp value. switched to a linear axis, where the
   arches (and their tops, the midpoint errors) read directly.
14. **screenshot tooling, not the app:** headless edge never finished a capture of the live benchmark chart
   (constant polling keeps its virtual clock from settling). the finished job is shown instead, reopened by id.

15. **a zip that only windows could read properly.** `Compress-Archive` (windows powershell 5.1) stored paths
   with `\`; `unzip` extracted flat files named `CandidateID_Workbench\src\...`. found by unzipping the package
   in a clean folder; the script now uses windows' bundled `tar`, and both `unzip` and Expand-Archive were
   re-checked.

16. **personal data in the version-control evidence.** every commit carried the candidate's personal email,
   and the brief says "no personal data". nothing had flagged it, because the email is not in any file, only
   in commit metadata, which goes into the git bundle and the readable commit log. found during the audit;
   author and committer were rewritten to a github no-reply address before packaging (history otherwise
   unchanged; the commit hashes quoted in these docs were updated to match).
17. **stale claims in the docs.** the overview checklist marked the demo as "candidate to add" although the
   brief explicitly accepts a timestamped screenshot walkthrough with logs, and it would have kept saying the
   handwritten pages were missing even after they were added. the backend now reports which evidence files
   are really in `docs/`, and the checklist ticks only those (a test pins that nothing is claimed when the
   files are absent). the effort log and this log also gave a session window that ended before the ui
   rebuild.
18. **the assistant's own capture script asked for an invalid job, twice.** the first real-time capture
   requested meshes totalling 20.0M nodes (job limit 20M), the second a 6M-element mesh (limit 5M per mesh).
   both times the app refused correctly and the script timed out waiting for a progress bar. the third
   attempt used 19.5M nodes in meshes of at most 4.5M elements.
19. **a noisy error for a valid request.** typing a very fine mesh list made the live solver fire, get a
   400 from the backend and show an error toast, although the right move is simply "use run as job". seen in
   the captured frames. the ui now skips the live request above the same 200,000-node limit and says so in
   the status bar. the limit messages also print `19,500,160` instead of `19500160`.
20. **the one-pdf workaround dropped the handwritten appendix.** the ≤ 1 mb submission pdf from prompt 8
   carried the report and a link to the zip, but the handwritten pages (4 of the 7 backprop points) would
   only have been inside the zip, behind a download. the packaging now turns photos in `docs/handwritten/`
   into a 2-page pdf (exif rotation applied, each page kept ≤ 300 kb) and appends it to the submission pdf.
   tested with synthetic pages, including noisy 12-megapixel photos with a rotate-90 exif flag: 2 a4 pages,
   585 kb, upright. the test images never entered the repo.
21. **the packaged report kept a blank candidate id.** only the submission pdf filled it in; `docs/report.pdf`
   in the zip still said "candidate id: ________". the package script now prints the packaged report with
   the id, and ends with a readiness check listing what the candidate still has to add.

verification in this pass, all from a fresh clone of `repo/workbench.bundle`: release build with zero
warnings, 78/78 tests, `wb_report fea` / `backprop` and `wb_datagen examples` regenerate files that are
byte-identical to the committed `results/` and `data/examples/`, 45 relative links in the package resolve,
no absolute paths or personal data in any text file, every view of the packaged app loads with no console
errors, and every csv export route returns a file.

## challenge test

`tests/test_backprop.cpp :: gradient_check_catches_a_wrong_derivative` deliberately uses the common slip
tanh' = 1 − h (instead of 1 − h²) and asserts that the finite-difference check rejects it (difference
> 1e-2). it does.

## candidate's own edits and review

*(to be completed by the candidate: list what you changed yourself after the ai session, what you
re-derived by hand, and which parts you can explain without notes. do not leave this empty.)*
