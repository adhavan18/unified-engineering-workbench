// performance view: serial vs worker-pool benchmark run as a backend job.

import { download, getJson, query, runJob } from '../api.js';
import { cell, fmt, jobFinished, jobProgress, jobStarted, setFacts, setStatus, showError, validateForm } from '../ui.js';
import { LinePlot } from '../plot.js';

const $ = (s) => document.querySelector(s);
let plot, lastJob = null;
export const plots = {};

export function initPerformance(status) {
  plot = new LinePlot($('#perf-plot'), { xLabel: 'worker threads p', yLabel: 'speedup T₁/Tₚ' });
  plots['perf-plot'] = plot;
  showSystem(status);
  $('#perf-form').addEventListener('submit', (e) => { e.preventDefault(); run(); });
  for (const btn of document.querySelectorAll('[data-perf-export]')) {
    btn.addEventListener('click', () => {
      if (lastJob) download(`/api/jobs/${lastJob}/export?name=${btn.dataset.perfExport}`);
    });
  }
}

function showSystem(status) {
  if (!status) return;
  const s = status.system;
  // only meaningful for the page load that Launch.exe itself triggered; a later
  // reload would count the idle time since launch
  const uiReadyMs = Date.now() - status.process_start_epoch_ms;
  const uiReady = uiReadyMs < 30000 ? `${uiReadyMs.toFixed(0)} ms (includes browser start)` : 'n/a: page was reloaded after launch';
  setFacts($('#sys-info'), [
    ['cpu', s.cpu],
    ['cores', `${s.physical_cores} physical / ${s.logical_cpus} logical`],
    ['ram', `${s.ram_gib.toFixed(1)} GiB`],
    ['compiler', s.compiler],
    ['build', `${s.build_type}: ${s.build_flags.trim()}`],
    ['launch → backend ready', `${status.ready_ms.toFixed(0)} ms`],
    ['launch → ui ready', uiReady],
  ]);
}

async function run() {
  const values = validateForm($('#perf-form'));
  if (!values) return;
  const button = $('#perf-run');
  button.disabled = true;
  jobStarted('starting benchmark…');
  try {
    const s = await runJob(`/api/perf/benchmark?${query(values)}`, jobProgress);
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

// reopens a finished benchmark job (e.g. ?view=performance&job=benchmark-3)
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
  const body = $('#perf-table tbody');
  body.replaceChildren();
  for (const row of r.rows) {
    const tr = document.createElement('tr');
    cell(tr, row.mode);
    cell(tr, row.mode === 'serial' ? '–' : String(row.workers));
    cell(tr, row.times_s.map((t) => t.toFixed(3)).join(' · '));
    cell(tr, row.median_s.toFixed(4));
    cell(tr, row.speedup.toFixed(3));
    cell(tr, row.efficiency.toFixed(3));
    cell(tr, row.mode === 'serial' ? 'reference' : fmt(row.max_abs_diff_C, 2));
    body.appendChild(tr);
  }

  const agree = $('#perf-agree');
  agree.replaceChildren();
  const n = document.createElement('div');
  n.className = `notice ${r.agreement_ok ? 'notice-sim' : 'notice-warn'}`;
  n.style.color = r.agreement_ok ? 'var(--ok)' : 'var(--err)';
  n.textContent = r.agreement_ok
    ? `serial and parallel agree: every max |ΔT| ≤ ${r.agreement_limit_C} °C across ${r.config.cases} cases × ${r.config.elements + 1} nodes.`
    : 'serial and parallel results differ by more than 1e-8 °C. this is a bug; see the activity log.';
  agree.appendChild(n);
  if (r.single_cpu) {
    const w = document.createElement('div');
    w.className = 'notice notice-warn';
    w.textContent = 'only one logical cpu is available: the parallel path ran, but speedup cannot be assessed on this machine.';
    agree.appendChild(w);
  }
  if (r.skipped_workers.length) {
    const w = document.createElement('div');
    w.className = 'notice notice-warn';
    w.textContent = `skipped p = ${r.skipped_workers.join(', ')}: more than the ${r.system.logical_cpus} logical cpus available.`;
    agree.appendChild(w);
  }

  const par = r.rows.filter((x) => x.mode === 'parallel');
  plot.setSeries([
    { name: 'measured', x: par.map((x) => x.workers), y: par.map((x) => x.speedup), markers: true, color: '#0a66d8' },
    { name: 'ideal', x: par.map((x) => x.workers), y: par.map((x) => x.workers), dash: true, color: '#8e8e93' },
  ]);
  for (const btn of document.querySelectorAll('[data-perf-export]')) btn.disabled = false;
}
