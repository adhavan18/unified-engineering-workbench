// overview: one card per task with a live result from the backend, and the
// brief's requirements mapped to the exact place in the app that meets them.

import { getJson } from '../api.js';
import { fmt, setFacts } from '../ui.js';

const $ = (s) => document.querySelector(s);

const TASKS = [
  { id: 'A', view: 'overview', title: 'interface & one-click launch', text: 'one app, five views, background jobs with progress and cancel, Launch.exe with no install.' },
  { id: 'B', view: 'geometry', title: 'cad / ansys data', text: 'ascii stl and nodal-temperature csv, parsed and validated in c++, shown together in 3d.' },
  { id: 'C', view: 'thermal', title: 'thermal fea', text: '1d steady conduction with linear elements, compared live with the exact solution.' },
  { id: 'D', view: 'performance', title: 'multicore', text: 'fixed-seed batch of solves, serial vs a bounded std::thread pool, 5 timed reps.' },
  { id: 'E', view: 'hardware', title: 'embedded integration', text: 'simulated mcu, fpga and jtag adapters with telemetry and a recoverable timeout.' },
  { id: 'F', view: 'backprop', title: 'backpropagation', text: 'hand-derived gradients in c++, checked by central differences, 100-step training.' },
];

// [requirement, view, selector to highlight, done?]
const CHECKS = [
  ['one interface with geometry, thermal analysis, performance and hardware views', 'overview', '.nav', true],
  ['file selection (buttons and drag-and-drop)', 'geometry', '#geo-wrap', true],
  ['parameter validation with inline messages, mirrored by the backend', 'thermal', '#fea-form', true],
  ['progress / status, responsive ui during computation', 'performance', '.statusbar', true],
  ['cancellation without corrupting results (jobs publish only on success)', 'performance', '#perf-run', true],
  ['actionable error messages with line numbers and fix-it hints', 'geometry', '#geo-wrap', true],
  ['result export: csv tables and png of every plot and the 3d view', 'thermal', '[data-csv="nodes"]', true],
  ['stl: rotate, pan, zoom, fit-to-view', 'geometry', '#geo-views', true],
  ['csv point cloud with temperature legend, units and node inspection', 'geometry', '.inspector', true],
  ['malformed / non-finite data rejected, empty files explained, size limits documented', 'geometry', '#geo-wrap', true],
  ['fea for N = 10, 20, 40, 80 vs T_exact, midpoint errors by fe interpolation, convergence, units, csv', 'thermal', '#fea-table', true],
  ['assumptions, units, boundary conditions and a debugging insight', 'thermal', 'details.more', true],
  ['serial vs parallel, p = 1/2/4, warm-up + 5 reps, median, speedup, efficiency, ≤ 1e-8 °C agreement', 'performance', '#perf-table', true],
  ['cpu, cores, ram, compiler, flags and launch-to-ready reported', 'performance', '#sys-info', true],
  ['block diagram, pin/voltage table, jtag shown as a debug interface', 'hardware', '#topology', true],
  ['mock discovery, capabilities, connect/disconnect, telemetry, recoverable timeout, labelled simulated', 'hardware', '#hw-devices', true],
  ['backprop forward pass, chain rule, one update, gradient check ≤ 1e-5, 100-step loss plot', 'backprop', '#bp-graph', true],
  ['handwritten derivation scans (two pages)', null, null, false],
  ['3–5 minute demo video (a timestamped screenshot walkthrough is in docs/walkthrough)', null, null, false],
];

export function initOverview(go) {
  const grid = $('#ov-tasks');
  grid.replaceChildren(...TASKS.map((t) => {
    const card = document.createElement('article');
    card.className = 'card task';
    card.tabIndex = 0;
    card.innerHTML = '<div class="row"><span class="letter"></span><h2></h2></div><p class="muted small" style="margin:0"></p><div class="proof">checking…</div>';
    card.querySelector('.letter').textContent = t.id;
    card.querySelector('h2').textContent = t.title;
    card.querySelector('p').textContent = t.text;
    card.querySelector('.proof').id = `proof-${t.id}`;
    const open = () => go(t.view === 'overview' ? 'geometry' : t.view);
    card.addEventListener('click', open);
    card.addEventListener('keydown', (e) => { if (e.key === 'Enter') open(); });
    return card;
  }));

  const list = $('#ov-checklist');
  list.replaceChildren(...CHECKS.map(([text, view, sel, done]) => {
    const row = document.createElement('div');
    row.className = 'check';
    row.innerHTML = `<span class="tick${done ? '' : ' todo'}">${done ? '✓' : '!'}</span><span></span><span class="where"></span>`;
    row.children[1].textContent = text;
    row.children[2].textContent = view ? `${view} →` : 'candidate to add';
    if (view) {
      row.style.cursor = 'pointer';
      row.addEventListener('click', () => {
        go(view);
        requestAnimationFrame(() => {
          const el = document.querySelector(`#view-${view} ${sel}`) || document.querySelector(sel);
          if (!el) return;
          el.scrollIntoView({ behavior: 'smooth', block: 'center' });
          el.classList.remove('flash');
          void el.offsetWidth;
          el.classList.add('flash');
        });
      });
    }
    return row;
  }));
}

const proof = (id, text, ok = true) => {
  const el = $(`#proof-${id}`);
  el.textContent = text;
  el.className = `proof${ok ? ' ok' : ''}`;
};

export async function refreshOverview(status) {
  if (status) {
    proof('A', `✓ backend ready ${status.ready_ms.toFixed(0)} ms after launch · loopback only`);
    $('#ov-conn').className = 'pill pill-ok';
    $('#ov-conn').textContent = 'backend running';
    setFacts($('#ov-session'), [
      ['address', location.host],
      ['backend ready', `${status.ready_ms.toFixed(0)} ms after process start`],
      ['cpu', status.system.cpu],
      ['cores', `${status.system.physical_cores} physical / ${status.system.logical_cpus} logical`],
      ['compiler', `${status.system.compiler} · ${status.system.build_type}`],
      ['version', status.version],
    ]);
  }
  proof('B', '✓ heat-sink stl + synthetic temperature field ready to load', true);
  getJson('/api/thermal/live?meshes=10,20,40,80').then((r) => {
    const m = r.meshes[3];
    proof('C', `✓ N = 80: midpoint error ${fmt(m.max_midpoint_error_C, 4)} °C, order ${m.observed_order.toFixed(3)}, peak ${fmt(r.peak_T_C, 4)} °C`);
  }).catch(() => proof('C', 'backend not reachable', false));
  getJson('/api/hardware/devices').then((r) => proof('E', `✓ ${r.devices.length} simulated devices: ${r.devices.map((d) => d.id).join(', ')}`))
    .catch(() => proof('E', 'backend not reachable', false));
  getJson('/api/backprop').then((r) => {
    const e = Math.max(...r.gradients.map((g) => g.abs_error));
    proof('F', `✓ gradient check ${r.gradients.filter((g) => g.pass).length}/4 pass, max error ${e.toExponential(1)}`);
  }).catch(() => proof('F', 'backend not reachable', false));
  let last = null;
  try { last = JSON.parse(sessionStorage.getItem('wb.lastBench') || 'null'); } catch { /* storage blocked */ }
  proof('D', last ? `✓ last run: ${last.speedup.toFixed(2)}× at p = ${last.p}, max |ΔT| ${last.diff} °C` : 'run the benchmark to see speedup (≈ 8 s)', !!last);
}
