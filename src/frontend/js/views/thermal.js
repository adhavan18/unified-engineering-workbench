// thermal analysis view: runs the 1d fe solver as a backend job.

import { download, query, runJob } from '../api.js';
import { cell, fmt, jobFinished, jobProgress, jobStarted, showError, validateForm } from '../ui.js';
import { LinePlot } from '../plot.js';

const $ = (s) => document.querySelector(s);
const BRIEF = { L: '1', A: '1e-4', k: '10', q: '10000', T0: '100', TL: '20', meshes: '10,20,40,80' };

let tempPlot, convPlot, lastJob = null;
export const plots = {};

export function initThermal() {
  tempPlot = new LinePlot($('#fea-plot'), { xLabel: 'x (m)', yLabel: 'temperature (°C)', xUnit: ' m', yUnit: ' °C' });
  convPlot = new LinePlot($('#conv-plot'), {
    xLabel: 'element size h (m)', yLabel: 'max error (°C)', logX: true, logY: true, xUnit: ' m', yUnit: ' °C',
  });
  plots['fea-plot'] = tempPlot;
  plots['conv-plot'] = convPlot;

  $('#fea-form').addEventListener('submit', (e) => { e.preventDefault(); run(); });
  $('#fea-reset').addEventListener('click', () => {
    for (const [k, v] of Object.entries(BRIEF)) $(`#fea-form [name="${k}"]`).value = v;
  });
  for (const btn of document.querySelectorAll('[data-export]')) {
    btn.addEventListener('click', () => {
      if (lastJob) download(`/api/jobs/${lastJob}/export?name=${btn.dataset.export}`);
    });
  }
}

export async function run() {
  const values = validateForm($('#fea-form'));
  if (!values) return;
  const button = $('#fea-run');
  button.disabled = true;
  jobStarted('starting thermal solve…');
  try {
    const s = await runJob(`/api/thermal/solve?${query(values)}`, jobProgress);
    if (s.state === 'cancelled') { jobFinished('thermal solve cancelled; previous results kept'); return; }
    lastJob = s.id;
    render(s.result, s.exports);
    jobFinished(`thermal solve done in ${s.elapsed_ms.toFixed(0)} ms`);
  } catch (e) {
    jobFinished('thermal solve failed');
    showError(e, 'thermal analysis');
  } finally {
    button.disabled = false;
  }
}

function render(r, exports) {
  const series = r.meshes.map((m) => ({
    name: `fe N = ${m.elements}`, x: m.x_m, y: m.T_C, markers: m.elements <= 20,
  }));
  series.push({ name: 'exact', x: r.exact.x_m, y: r.exact.T_C, color: '#6e6e73', dash: true });
  tempPlot.setSeries(series);

  const h = r.meshes.map((m) => m.h_m);
  convPlot.setSeries([
    { name: 'midpoint error', x: h, y: r.meshes.map((m) => m.max_midpoint_error_C), markers: true, color: '#e5484d' },
    { name: 'theory qh²/8k', x: h, y: r.meshes.map((m) => m.theory_midpoint_error_C), dash: true, color: '#6e6e73' },
    { name: 'nodal error', x: h, y: r.meshes.map((m) => Math.max(m.max_nodal_error_C, 1e-16)), markers: true, noLine: true, color: '#0a66d8' },
  ]);

  const body = $('#fea-table tbody');
  body.replaceChildren();
  r.meshes.forEach((m, i) => {
    const tr = document.createElement('tr');
    cell(tr, String(m.elements));
    cell(tr, fmt(m.h_m, 4));
    cell(tr, fmt(m.max_midpoint_error_C, 8));
    cell(tr, fmt(m.theory_midpoint_error_C, 8));
    cell(tr, fmt(m.max_nodal_error_C, 2));
    const order = r.convergence[i].observed_order;
    cell(tr, order === null ? '–' : order.toFixed(4));
    body.appendChild(tr);
  });

  for (const btn of document.querySelectorAll('[data-export]')) {
    btn.disabled = !exports.includes(btn.dataset.export);
    btn.title = btn.disabled ? 'too many nodes for a full csv; reduce N' : 'download csv';
  }
}
