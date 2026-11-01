# example data provenance

both example files were **self-created** for this assignment by `tools/datagen_main.cpp`
(`wb_datagen examples data/examples`). no third-party, customer or personal data is included.
licence: same as the rest of the submission (free to use for assessment).

## data/examples/heatsink_ascii.stl

| | |
|---|---|
| what | a finned heat sink: 60 × 40 × 5 mm base plate with 7 fins, 2 mm thick, 25 mm tall |
| format | ascii stl, one solid named `heatsink_60x40mm_self_created`, 96 triangles |
| units | **millimetres** (stl stores no unit; select "mm" in the ui) |
| how made | each body is an axis-aligned box written as 12 outward-facing triangles (counter-clockwise seen from outside). fins sit on the base as separate closed boxes, which is common in simple exports and fine for visualisation, but the file is not one watertight solid |
| export notes | equivalent to "save as → stl, ascii" in a cad tool. binary stl is detected and rejected with instructions |

## data/examples/rod_nodal_temperatures_synthetic.csv

> **synthetic data. this is not an ansys export.** it was generated from the analytical solution so that
> the import path can be tested without an ansys licence.

| | |
|---|---|
| what | the task c rod (L = 1 m, 10 mm × 10 mm section, A = 1e-4 m², k = 10 W/(m·K), q = 10⁴ W/m³, T(0) = 100 °C, T(L) = 20 °C) sampled on 81 stations × 5 × 5 cross-section nodes = 2,025 nodes |
| values | T(x) = 100 − 80x + 500 x(1 − x) °C. lateral faces are insulated, so T is uniform across each cross-section. peak 188.2 °C at x = 0.42 m |
| schema | `node_id,x_m,y_m,z_m,temperature_C`: integer id ≥ 0, coordinates in metres, temperature in °C, comma-separated, `.` decimal point, 6 decimals |

## mapping a real ansys mechanical export onto the schema

native ansys project/result files (.wbpj, .rst, .rth) are out of scope. the supported path is a text export
of a nodal temperature result:

1. in mechanical, right-click the temperature result → **export → export text file**.
   the export is tab-separated with headers like `Node Number`, `X Location (m)`, `Y Location (m)`,
   `Z Location (m)`, `Temperature (°C)` (exact header text depends on the version and unit system).
2. make sure the unit system is metres and °C (mechanical "metric (m, kg, n, s, v, a)" with celsius),
   or convert the columns.
3. replace the header with `node_id,x_m,y_m,z_m,temperature_C` and the tabs with commas
   (e.g. in excel: save as "csv utf-8"; the reader accepts the utf-8 bom).

this mapping is documented from ansys' export format, not tested against a live ansys install
(no licence was used for this assignment).

## malformed inputs for tests

`tests/fixtures/` holds hand-written broken files (nan values, truncated facets, wrong headers, empty and
header-only files) used by the negative tests.
