# screenshot walkthrough with logs

an equivalent of the demo video, captured on 2026-11-01 between 11:59 and 12:06 from the dev build (same code
as the packaged exe) with headless microsoft edge at 1440 px wide. each `?view=<name>&demo` url presses the
same controls a person would (see `runDemo` in `src/frontend/js/main.js`). [activity_log.txt](activity_log.txt)
is the backend log for the whole capture.

> the demo video is still the candidate's to record. a 3–5 minute narrated screen recording of the same
> steps is the strongest evidence, and it shows the live parts (sliders, streaming charts) that stills cannot.

| capture | frame | what it shows |
|---|---|---|
| 11:59:30 | [01_overview.png](01_overview.png) | one card per task with a live result from the backend (fea order 2.000, 3 simulated devices, gradient check 4/4) and the requirement checklist mapping every bullet of the brief to a place in the app |
| 11:59:36 | [02_geometry.png](02_geometry.png) | heat-sink stl (read in mm, converted to m in c++) overlaid at 25 % opacity on its synthetic temperature field (6,363 nodes, inferno map, legend in °C); the hottest node (id 326, 85 °C) is pinned in the inspector |
| 11:59:44 | [03_thermal.png](03_thermal.png) | brief values: peak 188.2 °C at x = 0.42 m; heat out of the ends 0.42 + 0.58 = 1 W = qAL; error arches along x; midpoint errors 1.25 → 0.01953125 °C equal to qh²/8k; observed order 2.0000 |
| 12:04:57 | [04_performance.png](04_performance.png) | finished job: 200 cases × 20,000 elements, every measured run plotted, median bars with min–max whiskers, speedup 0.99 / 1.86 / 3.63 at p = 1 / 2 / 4, serial vs parallel max \|ΔT\| = 0 °C. the job was started through the api and reopened with `?view=performance&job=…` because a headless screenshot cannot wait for a live run |
| 12:06:08 | [05_hardware.png](05_hardware.png) | topology coloured by link state with animated links for streaming devices; jtag0 got an injected timeout (421 ms), recovered on retry and now streams vref / tck / idcode; mcu0 streams with sparklines; fpga0 untouched |
| 12:05:18 | [06_backprop.png](06_backprop.png) | computation graph at step 1 with forward values and red gradients, gradient check 4/4 (max error 1.1e-10), loss 1.1825 → 0.6858 after the handwritten step, parameter trajectories |
| 12:05:26 | [07_error_handling.png](07_error_handling.png) | a csv with a nan temperature is rejected: file name, line 3 and a fix-it hint; the results panel shows "rejected" and nothing is loaded |

the log also shows benchmark-1, started by a capture attempt that hung on the live chart and was killed; its
result is not used anywhere.
