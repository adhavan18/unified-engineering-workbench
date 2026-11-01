# unified engineering workbench

a small engineering workbench for windows 11 x64. it imports cad/ansys data, solves a 1d thermal fe problem,
benchmarks serial vs multicore batch solves, drives simulated embedded devices and checks a handwritten
backprop step. all backend logic, numerics, file parsing and concurrency are c++17. the ui is plain
html/css/js served locally by the same exe.

> candidate id, hours spent and the handwritten backprop pages are filled in by the candidate.
> see [docs/effort_log.md](docs/effort_log.md) and [docs/backprop_derivation.md](docs/backprop_derivation.md).

---

## one-click start

1. extract the zip anywhere (a path with spaces is fine).
2. double-click **`Launch.exe`**.
3. your default browser opens `http://127.0.0.1:<port>/`. a console window shows the activity log;
   closing it (or pressing **quit** in the ui) stops the app.

no install, no admin rights, no internet, no first-run build. the exe is statically linked and only uses
dlls that ship with windows. the server binds to `127.0.0.1` only, so nothing is reachable from the network.

*if windows smartscreen says "windows protected your pc"* (normal for an unsigned exe from a zip):
click **more info → run anyway**. no admin rights are needed for that.

### from vs code (or antigravity)

1. **file → open folder…** and pick the extracted folder (the one containing `Launch.exe`).
2. open **run and debug** (ctrl+shift+d), pick **"run workbench (packaged Launch.exe)"**, press **f5**.

the configuration uses the `node-terminal` type from vs code's built-in javascript debugger, so **no
extension needs to be installed**. alternatively: **terminal → run task → "run workbench"**. the editor
is optional; `Launch.exe` never needs it.

## what's inside

| view | task | what it does |
|---|---|---|
| geometry | b | ascii stl (rotate / pan / zoom / fit) and nodal-temperature csv as a coloured point cloud with legend (°C) and click-to-inspect |
| thermal analysis | c | 1d steady fe rod solver, N = 10/20/40/80, exact comparison, midpoint errors, convergence, csv/png export |
| performance | d | fixed-seed batch of rod solves, serial vs bounded worker pool (1/2/4 threads), 5 reps, median, speedup, efficiency, 1e-8 °C agreement |
| hardware | e | simulated mcu, fpga and jtag adapters: discovery, capabilities, connect/disconnect, telemetry, injected timeout + recovery |
| backprop | f | forward pass, analytic vs central-difference gradients, one update, 100-step loss curve |

long computations run as background jobs with a progress bar and **cancel** in the status bar. a cancelled
job never publishes partial results, so earlier results and exports stay intact.

## architecture

```
Launch.exe ──► src/app/main.cpp        starts the server on 127.0.0.1, opens the browser, measures launch-to-ready
                    │
               src/server/             transport only: no engineering logic here
                 http.*                minimal http/1.1 over winsock, one thread per request
                 api.*                 routes ↔ core calls, input validation, json / binary replies
                 jobs.*                cancellable background jobs, results published atomically
                    │
               src/core/               engineering logic, no http or ui knowledge
                 fea1d.*               assembly, dirichlet bcs, thomas solver, exact solution, error norms
                 batch.* worker_pool.* case generator, serial + pooled batch runs, benchmark harness
                 stl.* nodal_csv.*     strict parsers with line numbers and fix-it hints
                 hardware.*            device adapter interface + three mocks
                 backprop.*            hand-derived gradients, central differences, training loop
                 sysinfo.*             cpu / cores / ram / compiler for reports
                    │
src/frontend/  (served as app/www)     index.html, css, js: tabs, webgl viewer, canvas plots
tools/                                 wb_report (regenerates results/), wb_datagen (example + size-test data),
                                       experiments/ (timing probes used in the performance analysis)
tests/                                 65 unit, numerical, negative and api tests (no external framework)
```

the browser never computes anything: every number shown comes from the c++ backend. stl and csv uploads are
sent as raw bytes; geometry comes back as a compact binary payload (json header + float arrays) rather than
megabytes of json.

## supported formats

**ascii stl** (`solid … facet normal … outer loop … vertex ×3 … endloop … endfacet … endsolid`). case-insensitive,
crlf ok, several solids per file ok. no unit is stored in stl; choose mm / m / in in the ui. rejected with a
line number and hint: empty or whitespace-only files, binary stl (including binary files whose header starts
with "solid"), facets with ≠ 3 vertices, non-numeric or nan/inf coordinates, truncated files, files with no
triangles. zero-area triangles are drawn and counted.

**nodal temperature csv** with exactly the header `node_id,x_m,y_m,z_m,temperature_C` (metres, degrees
celsius). utf-8 bom, crlf, spaces around values and blank lines are accepted. rejected with a line number:
wrong/missing/extra columns, non-integer or duplicate node ids, nan/inf, temperatures below −273.15 °C,
quoted fields, header-only and empty files. see [data/PROVENANCE.md](data/PROVENANCE.md) for how a real ansys
mechanical export maps onto this schema.

**tested file sizes** ([results/file_size_tests.csv](results/file_size_tests.csv)): limits are 256 MB per file,
2,000,000 triangles and 5,000,000 nodes.

| file | size | items | backend parse | through the ui (upload + parse + render) |
|---|---|---|---|---|
| stl | 15 MB | 120k triangles | 0.10 s | – |
| stl | 156 MB | 1.2M triangles | 1.1 s | 114 MB / 1.2M tris: 4.9 s |
| stl | 282 MB | 2.16M triangles | rejected (size limit) | – |
| csv | 4 MB | 100k nodes | 0.07 s | – |
| csv | 42 MB | 1M nodes | 1.1–1.6 s | 33 MB / 1M nodes: 3.4 s, picking 1 node ≈ instant |
| csv | 171 MB | 4M nodes | 6.4 s | – |

## build from source (cmake)

prerequisites: cmake ≥ 3.20, ninja, and a c++17 compiler. the submitted build used the toolchain in
[toolchain.lock](toolchain.lock) (gcc 16.2 from winlibs, `winget install BrechtSanders.WinLibs.POSIX.UCRT`).
the CMakeLists has an msvc branch (static crt, /W4), but only the gcc build was actually built and tested.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build\Launch.exe
```

a dev build finds the frontend in `src/frontend` and the examples in `data/` automatically.

| command | purpose |
|---|---|
| `build\wb_tests.exe [filter]` | run the test suite (or a subset, e.g. `wb_tests fea`) |
| `build\wb_report.exe all results 400 20000` | regenerate fea, backprop and benchmark evidence in `results/` |
| `build\wb_report.exe filesize results <files…>` | time the parsers on large inputs |
| `build\wb_datagen.exe examples data\examples` | regenerate the example stl and csv |
| `build\Launch.exe --ready-check` | print launch-to-ready time and exit |
| `powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -CandidateId <id>` | build, test and zip |

## results at a glance

- **fea:** max midpoint errors 1.25, 0.3125, 0.078125, 0.01953125 °C for N = 10, 20, 40, 80, exactly q h²/(8k);
  observed order 2.0000; nodal errors ≤ 1.2e-12 °C ([results/fea_convergence.csv](results/fea_convergence.csv)).
- **performance** (i5-1240P, 400 cases × 20,000 elements, idle machine): speedup 1.00 / 1.86–1.91 / 3.66–3.70 at
  p = 1 / 2 / 4, efficiency ≈ 0.92 at p = 4, serial vs parallel max |ΔT| = 0 °C. a noisier session 1 is kept and
  explained in the report.
- **launch-to-ready:** 54–68 ms from process creation to listening socket (5 runs,
  [results/launch_to_ready.csv](results/launch_to_ready.csv)); browser start-up comes on top.
- **backprop:** all four gradients agree with central differences to ≤ 1.2e-10; loss 1.1825 → 0.6858 after the
  handwritten step, ~1e-20 after 101 steps.
- **tests:** 65 / 65 pass ([docs/test_output.txt](docs/test_output.txt)).

## known limitations

- 1d, steady, constant-property, uniform-q rod only; dirichlet ends only (no convection or flux bcs).
- stl: ascii only; meshes are drawn flat-shaded without edge extraction or repair. the "units" selector labels
  values, it does not convert them. no mesh-to-result registration (out of scope per the brief).
- csv: exactly one schema; ansys text exports must be renamed / comma-separated first (see provenance notes).
  float32 positions on the gpu (fine for display; inspection shows the parsed doubles' float32 view).
- the performance numbers come from one laptop with hybrid p/e cores and a turbo budget; single-thread
  baselines there vary by up to 2× between repetitions. treat speedups as ±5 %.
- hardware devices are **mocks**. nothing talks to usb, serial or jtag; real integration needs the plan in
  [docs/embedded_design.md](docs/embedded_design.md).
- the http server is for one local user (loopback only, no auth, connection: close). not a production server.
- unsigned exe: smartscreen may ask for "run anyway" the first time.

## repository map

`src/` code · `tests/` tests + malformed fixtures · `data/` example inputs + provenance · `results/` raw csv +
labelled svg plots · `docs/` report, embedded design, backprop study sheet, ai-use log, ownership, walkthrough ·
`repo/` (in the zip) git bundle + readable commit log · `.vscode/` run configurations.
