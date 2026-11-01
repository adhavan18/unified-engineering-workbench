# screenshot walkthrough with logs

an equivalent of the demo video, captured on 2026-11-01 from the dev build (same code as the packaged exe)
with headless microsoft edge at 1440 px wide. each frame was produced by opening a `?view=<name>&demo` url,
which presses the same buttons a person would (see `runDemo` in `src/frontend/js/main.js`).
[activity_log.txt](activity_log.txt) is the backend log for the whole capture (one absolute path was made
relative when saving it, as marked in the file).

> the demo video is still the candidate's to record. a 3–5 minute screen recording that walks through the
> same six steps, narrated, is the best evidence. use this page as the script.

| time (local) | frame | what it shows | log lines |
|---|---|---|---|
| 10:50:01 | – | `Launch.exe` started; backend ready **44.8 ms** after process creation | `listening on …`, `launch-to-ready …` |
| 10:50:35 | [01_geometry.png](01_geometry.png) | example heat-sink stl (96 triangles, 60 × 40 × 30 mm) and the synthetic rod csv (2,025 nodes) as a viridis point cloud with legend in °C; the hottest node (id 851, x = 0.425 m, 188.1875 °C) is inspected | `stl … parsed: 96 triangles`, `nodal csv … 2025 nodes, T 20 .. 188.1875 C` |
| 10:50:40 | [02_thermal.png](02_thermal.png) | fe solutions for N = 10/20/40/80 against the exact curve; error table 1.25 → 0.01953125 °C, observed order 2.0000; convergence plot | `thermal job thermal-1 started, N = 10,20,40,80` |
| 10:51:33 | [03a_performance_running.png](03a_performance_running.png) | benchmark running: progress "run 0 of 24" and the **cancel** button in the status bar; the run button is disabled while it runs | `benchmark job … started: 200 cases x 20000 elements` |
| 10:52:13 | [03b_performance_done.png](03b_performance_done.png) | finished job: serial 0.282 s, p = 1/2/4 speedup 1.006 / 1.992 / 3.643, efficiency 0.911 at p = 4, agreement max \|ΔT\| = 0 °C, machine info. this job was started through the api (`curl -X POST /api/perf/benchmark`), then reopened in the ui with `?view=performance&job=benchmark-4`, because the headless screenshot cannot wait for a 6 s run | `benchmark job benchmark-4 started …` |
| 10:51:00 | [04_hardware.png](04_hardware.png) | three simulated devices with capabilities; jtag0 with `timeout_once` → connect **times out (415 ms)** → retry connect → connected → telemetry (vref 3.302 V, idcode 0x0362d093 simulated); mcu0 telemetry | `jtag0 connect: timeout …`, `jtag0 connect: ok …`, `jtag0 telemetry: ok …` |
| 10:51:07 | [05_backprop.png](05_backprop.png) | forward pass, all four gradients pass the central-difference check (≤ 1.13e-10), one update, loss curve over 101 steps | – |
| 10:52:38 | [06_error_handling.png](06_error_handling.png) | a malformed csv (nan temperature) is rejected with file name, **line 3** and a fix-it hint; nothing is loaded | `POST /api/results/nodal-csv -> 422: broken_example.csv: temperature_C is not finite ('NaN')` |

capture times are from the capture script's clock, log times from the backend; both are the same machine.
benchmark-2 and benchmark-3 in the log were started by capture attempts whose page closed before they
finished (the screenshot fired at page load). the backend kept running them; they are not used anywhere.
