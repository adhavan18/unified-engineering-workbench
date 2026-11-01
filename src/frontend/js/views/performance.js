// performance view: serial vs worker-pool benchmark as a backend job, with
// every run's time streamed into a live chart while it runs.

import { download, getJson, query, runJob } from '../api.js';
import {
  cell, chips, fmt, jobFinished, jobProgress, jobStarted, setFacts, setKpis, setStatus, showError, validateForm,
} from '../ui.js';
import { BarChart, LinePlot, PALETTE } from '../plot.js';

const $ = (s) => document.querySelector(s);
export const plots = {};
let runsPlot, barPlot, speedPlot, lastJob = null, logicalCpus = 1;

const labelOf = (row, workers) => (row === 0 ? 'serial' : `p = ${workers}`);

export function initPerformance(status) {
  runsPlot = new LinePlot($('#runs-plot'), { xLabel: 'run number', yLabel: 'time (s)', yUnit: ' s' });
  barPlot = new BarChart($('#bar-plot'), { yLabel: 'median time (s)', yUnit: ' s' });
  speedPlot = new LinePlot($('#speed-plot'), { xLabel: 'worker threads p', yLabel: 'speedup T₁/Tₚ' });
  Object.assign(plots, { 'runs-plot': runsPlot, 'bar-plot': barPlot, 'speed-plot': speedPlot });
  if (status) {
    logicalCpus = status.system.logical_cpus;
    const s = status.system;
    setFacts($('#sys-info'), [
      ['cpu', s.cpu],
      ['cores', `${s.physical_cores} physical / ${s.logical_cpus} logical`],
      ['ram', `${s.ram_gib.toFixed(1)} GiB`],
      ['compiler', s.compiler],
      ['flags', s.build_flags.trim()],
      ['launch → ready', `${status.ready_ms.toFixed(0)} ms`],
    ]);
  }
  const choices = [1, 2, 3, 4, 6, 8, 12, 16].filter((n) => n <= Math.max(4, logicalCpus));
  chips($('#worker-chips'), choices, [1, 2, 4], (picked) => { $('#workers').value = picked.join(','); });
  $('#perf-run').addEventListener('click', run);
  for (const btn of document.querySelectorAll('[data-perf-export]')) {
    btn.addEventListener('click', () => { if (lastJob) download(`/api/jobs/${lastJob}/export?name=${btn.dataset.perfExport}`); });
  }
  setKpis($('#perf-kpis'), [
    { label: 'logical cpus', value: String(logicalCpus), foot: 'worker counts above this are skipped' },
    { label: 'speedup at p = 4', value: '–', foot: 'run the benchmark' },
    { label: 'efficiency at p = 4', value: '–', foot: '(T₁/Tₚ)/p' },
    { label: 'serial vs parallel', value: '–', foot: 'max |ΔT| must be ≤ 1e-8 °C' },
  ]);
}

function drawRuns(runs) {
  const byRow = new Map();
  runs.forEach((r, n) => {
    const key = r.row;
    if (!byRow.has(key)) byRow.set(key, { label: labelOf(r.row, r.workers), warm: { x: [], y: [] }, meas: { x: [], y: [] } });
    const s = byRow.get(key);
    (r.measured ? s.meas : s.warm).x.push(n + 1);
    (r.measured ? s.meas : s.warm).y.push(r.time_s);
  });
  const series = [];
  [...byRow.entries()].sort((a, b) => a[0] - b[0]).forEach(([row, s]) => {
    const color = PALETTE[(row + 3) % PALETTE.length];
    series.push({ name: s.label, x: s.meas.x, y: s.meas.y, color, markers: true, noLine: true, markerSize: 4.5 });
    if (s.warm.x.length) series.push({ name: `${s.label} warm-up`, x: s.warm.x, y: s.warm.y, color, markers: true, hollow: true, noLine: true, markerSize: 4.5 });
  });
  runsPlot.setSeries(series, { keepZoom: true });
}

async function run() {
  const values = validateForm($('#perf-form'));
  if (!values) return;
  const button = $('#perf-run');
  button.disabled = true;
  runsPlot.setSeries([]);
  jobStarted('starting benchmark…');
  try {
    const s = await runJob(`/api/perf/benchmark?${query(values)}`, (st) => {
      jobProgress(st);
      if (st.partial?.runs) drawRuns(st.partial.runs);
    });
    if (s.state === 'cancelled') { jobFinished('benchmark cancelled; no partial timings were kept'); return; }
    lastJob = s.id;
    render(s.result);
    jobFinished(`benchmark done in ${(s.elapsed_ms / 1000).toFixed(1)} s`);
  } catch (e) {
    jobFinished('benchmark failed');
    showError(e, 'benchmark');
  } finally {
    button.disabled = false;
  }
}

// reopens a finished benchmark job (?view=performance&job=benchmark-3)
export async function showJob(id) {
  try {
    const s = await getJson(`/api/jobs/${encodeURIComponent(id)}`);
    if (s.state !== 'done') return;
    lastJob = s.id;
    render(s.result);
    setStatus(`showing finished job ${s.id}`);
  } catch (e) {
    showError(e, 'reopen job');
  }
}

function render(r) {
  // the final runs chart is rebuilt from the published result (warm-ups are not part of it)
  const runs = [];
  const reps = r.config.repetitions;
  for (let k = 0; k < reps; k++) r.rows.forEach((row, i) => runs.push({ row: i, workers: row.workers, measured: true, time_s: row.times_s[k] }));
  drawRuns(runs);

  barPlot.setBars(r.rows.map((row, i) => ({
    label: labelOf(i, row.workers), value: row.median_s, lo: Math.min(...row.times_s), hi: Math.max(...row.times_s),
    color: PALETTE[(i + 3) % PALETTE.length], note: i === 0 ? 'baseline' : `${row.speedup.toFixed(2)}×`,
  })));
  const par = r.rows.filter((x) => x.mode === 'parallel');
  speedPlot.setSeries([
    { name: 'measured', x: par.map((x) => x.workers), y: par.map((x) => x.speedup), markers: true, color: '#2563eb' },
    { name: 'ideal', x: par.map((x) => x.workers), y: par.map((x) => x.workers), dash: true, color: '#8e8e93' },
  ]);

  const p4 = par.find((x) => x.workers === 4) || par[par.length - 1];
  const worst = Math.max(...par.map((x) => x.max_abs_diff_C));
  try {
    sessionStorage.setItem('wb.lastBench', JSON.stringify({ speedup: p4.speedup, p: p4.workers, diff: worst === 0 ? '0' : worst.toExponential(1) }));
  } catch { /* storage may be blocked; the overview just shows "run it" */ }
  setKpis($('#perf-kpis'), [
    { label: 'serial median', value: r.rows[0].median_s.toFixed(3), unit: 's', foot: `${r.config.cases} cases × ${r.config.elements.toLocaleString()} elements` },
    { label: `speedup at p = ${p4.workers}`, value: p4.speedup.toFixed(2), unit: '×', foot: `ideal ${p4.workers}×` },
    { label: `efficiency at p = ${p4.workers}`, value: (p4.efficiency * 100).toFixed(0), unit: '%', foot: '(T₁/Tₚ)/p' },
    { label: 'serial vs parallel', value: worst === 0 ? '0' : worst.toExponential(1), unit: '°C', foot: `max |ΔT|, limit ${r.agreement_limit_C} °C`, ok: r.agreement_ok },
  ]);

  const body = $('#perf-table tbody');
  body.replaceChildren();
  for (const row of r.rows) {
    const tr = document.createElement('tr');
    cell(tr, row.mode);
    cell(tr, row.mode === 'serial' ? '–' : String(row.workers));
    cell(tr, row.times_s.map((t) => t.toFixed(3)).join(' · '));
    cell(tr, row.median_s.toFixed(4));
    cell(tr, `${row.speedup.toFixed(3)}×`);
    cell(tr, `${(row.efficiency * 100).toFixed(1)} %`);
    cell(tr, row.mode === 'serial' ? 'reference' : fmt(row.max_abs_diff_C, 2), row.mode === 'serial' ? '' : row.max_abs_diff_C <= r.agreement_limit_C ? 'ok' : 'err');
    body.appendChild(tr);
  }

  const agree = $('#perf-agree');
  agree.replaceChildren();
  const add = (cls, text) => { const n = document.createElement('div'); n.className = `notice ${cls}`; n.style.marginBottom = '10px'; n.textContent = text; agree.appendChild(n); };
  add(r.agreement_ok ? 'notice-ok' : 'notice-err', r.agreement_ok
    ? `serial and parallel agree: max |ΔT| ≤ ${r.agreement_limit_C} °C over ${r.config.cases} cases × ${(r.config.elements + 1).toLocaleString()} nodes.`
    : 'serial and parallel results differ by more than 1e-8 °C. this is a bug: see the activity log.');
  if (r.single_cpu) add('notice-warn', 'only one logical cpu: the parallel path ran, but speedup cannot be assessed on this machine.');
  if (r.skipped_workers.length) add('notice-warn', `skipped p = ${r.skipped_workers.join(', ')}: more than the ${r.system.logical_cpus} logical cpus available.`);
  for (const btn of document.querySelectorAll('[data-perf-export]')) btn.disabled = false;
}
