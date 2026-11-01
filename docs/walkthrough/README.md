# screenshot walkthrough with logs

this is the "equivalent timestamped screenshot walkthrough with logs" that the brief accepts in place of the
demo video. it was captured on 2026-11-01 from the dev build (same code as the packaged `Launch.exe`) by
[scripts/capture_walkthrough.mjs](../../scripts/capture_walkthrough.mjs). the script opens the real app in
headless microsoft edge at 1440 × 900 and presses the same controls a person would, **in real time**, so
the frames show live states: a job mid-progress, a cancel, a benchmark streaming its runs.

[activity_log.txt](activity_log.txt) is the backend log for the same launch. the app ran twice through the
script on that launch (13:57 and 13:58; the frames below are from the second pass), so most events
appear twice in the log.

to reproduce:

```bat
build\Launch.exe --no-browser --port 8765
node scripts\capture_walkthrough.mjs http://127.0.0.1:8765 docs\walkthrough
```

| capture | frame | brief requirement | what it shows |
|---|---|---|---|
| 13:58:32 | [01_overview.png](01_overview.png) | a: one coherent interface | one card per task with a live result from the backend; session facts (cpu, cores, compiler, launch-to-ready) |
| 13:58:32 | [01b_checklist.png](01b_checklist.png) | all | every bullet of the brief mapped to the place in the app. docs evidence (handwritten pages, video) is ticked only if the file is actually in `docs/` |
| 13:58:34 | [02_geometry.png](02_geometry.png) | b: stl + csv, legend, units, inspection | heat-sink stl (read in mm, converted to m in c++) over its synthetic temperature field, inferno map with a °C legend; the hottest node (id 326, 85 °C) pinned in the inspector |
| 13:58:36 | [03_thermal.png](03_thermal.png) | c: fea vs exact, convergence | brief values: peak 188.2 °C at x = 0.42 m; heat out of the ends 0.42 + 0.58 = 1 W = qAL; midpoint error at N = 80 equals qh²/8k; observed order 2.000 |
| 13:58:36 | [04_validation.png](04_validation.png) | a: parameter validation | k = −5 typed: the field is flagged inline ("enter a positive number"); the last valid solution stays on screen |
| 13:58:38 | [05_job_progress.png](05_job_progress.png) | a: progress, responsive ui | a 19.5-million-node study runs as a background job; progress and cancel in the status bar while the page stays usable |
| 13:58:39 | [06_job_cancelled.png](06_job_cancelled.png) | a: cancellation without corrupting results | cancelled: the job publishes nothing, so the previous results, plots and csv exports are unchanged |
| 13:58:43 | [07_benchmark_live.png](07_benchmark_live.png) | d: real parallel work | benchmark running (run 12 of 24): every timed run, warm-ups hollow, streams into the chart as it finishes |
| 13:58:47 | [08_benchmark_done.png](08_benchmark_done.png) | d: median, speedup, efficiency, agreement | 200 cases × 20,000 elements: median bars with min–max whiskers, speedup vs ideal, efficiency at p = 4, serial vs parallel max \|ΔT\| = 0 °C; machine and flags listed |
| 13:58:50 | [09_hardware.png](09_hardware.png) | e: three device types, recoverable failure | jtag0 got an injected timeout, faulted, recovered on retry and now streams; mcu0 streams; fpga0 idle; every card labelled simulated, jtag marked "no: debug interface only" |
| 13:58:53 | [10_backprop.png](10_backprop.png) | f: c++ gradient check, loss | computation graph at step 1 with forward values and gradients; gradient check 4/4; loss after the handwritten step |
| 13:58:55 | [11_error_nan.png](11_error_nan.png) | b: reject non-finite data | a csv with a nan temperature is rejected: file name, line 3 and a fix-it hint; nothing is loaded |
| 13:58:58 | [12_error_empty.png](12_error_empty.png) | b: explain empty files | an empty stl gets "the file is empty (0 bytes)" and what to do, instead of a silent failure |
| 13:59:00 | [13_activity_log.png](13_activity_log.png) | a: status, logs | the activity-log drawer: the backend log streamed from Launch.exe (job start, cancel, timeout, recovery, rejections) |

a narrated 3–5 minute screen recording would show the live parts (sliders re-solving, streaming charts)
better than stills. if one is added as `docs/demo_video.mp4`, the overview checklist picks it up.
