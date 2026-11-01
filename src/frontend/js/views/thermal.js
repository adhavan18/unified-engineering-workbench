// thermal fea view: every slider move re-runs the c++ mesh study (debounced),
// so plots and tables follow the inputs live. very fine meshes go through a
// cancellable background job instead.

import { download, getJson, query, runJob } from '../api.js';
import {
  bindSliders, cell, chips, debounce, fmt, jobFinished, jobProgress, jobStarted, setField, setKpis, setStatus,
  showError, validateForm,
} from '../ui.js';
import { LinePlot, PALETTE } from '../plot.js';

const $ = (s) => document.querySelector(s);
const BRIEF = { k: '10', q: '10000', T0: '100', TL: '20', L: '1', A: '1e-4', meshes: '10,20,40,80' };
const MESH_CHOICES = [5, 10, 20, 40, 80, 160, 320];
const LIVE_NODE_LIMIT = 200000;  // same as kLiveNodeLimit in src/server/api.cpp, which still enforces it

export const plots = {};
let tempPlot, errPlot, convPlot;
let inflight = 0;
let lastValues = null;

export function initThermal() {
  tempPlot = new LinePlot($('#fea-plot'), { xLabel: 'x (m)', yLabel: 'temperature (°C)', xUnit: ' m', yUnit: ' °C' });
  errPlot = new LinePlot($('#err-plot'), { xLabel: 'x (m)', yLabel: '|error| (°C)', xUnit: ' m', yUnit: ' °C' });
  convPlot = new LinePlot($('#conv-plot'), { xLabel: 'element size h (m)', yLabel: 'max error (°C)', logX: true, logY: true, xUnit: ' m', yUnit: ' °C' });
  Object.assign(plots, { 'fea-plot': tempPlot, 'err-plot': errPlot, 'conv-plot': convPlot });

  const form = $('#fea-form');
  form.addEventListener('submit', (e) => e.preventDefault());
  const live = debounce(solveLive, 60);
  bindSliders(form, live);
  $('#meshes').addEventListener('input', () => { syncChips(); live(); });
  $('#fea-brief').addEventListener('click', () => {
    for (const [k, v] of Object.entries(BRIEF)) setField(form, k, v);
    syncChips();
    solveLive();
  });
  form.querySelector('[name="A"]').addEventListener('input', live);
  $('#fea-job').addEventListener('click', runAsJob);
  for (const b of document.querySelectorAll('[data-csv]')) {
    b.addEventListener('click', () => {
      if (lastValues) download(`/api/thermal/export?what=${b.dataset.csv}&${query(lastValues)}`);
    });
  }
  syncChips();
}

function syncChips() {
  const current = $('#meshes').value.split(',').map((s) => +s.trim()).filter((n) => n > 0);
  chips($('#mesh-chips'), MESH_CHOICES, current, (picked) => {
    const custom = current.filter((n) => !MESH_CHOICES.includes(n));
    $('#meshes').value = [...new Set([...picked, ...custom])].sort((a, b) => a - b).join(',');
    solveLive();
  });
}

export async function solveLive() {
  const values = validateForm($('#fea-form'));
  if (!values) { setStatus('fix the highlighted input to update the solution'); return; }
  const nodes = values.meshes.split(',').reduce((s, n) => s + (+n + 1), 0);
  if (nodes > LIVE_NODE_LIMIT) {
    ++inflight;  // drop any reply still in flight for the previous inputs
    setStatus(`${nodes.toLocaleString()} nodes is too fine for live updates (limit ${LIVE_NODE_LIMIT.toLocaleString()}): press "run as job" for progress and cancel`);
    return;
  }
  const ticket = ++inflight;
  const t0 = performance.now();
  try {
    const r = await getJson(`/api/thermal/live?${query(values)}`);
    if (ticket !== inflight) return;  // a newer request superseded this one
    lastValues = values;
    render(r);
    const n = r.meshes.reduce((s, m) => s + m.elements, 0);
    setStatus(`solved ${r.meshes.length} meshes (${n.toLocaleString()} elements) in ${(performance.now() - t0).toFixed(0)} ms, round trip included`);
  } catch (e) {
    if (ticket === inflight) showError(e, 'thermal');
  }
}

async function runAsJob() {
  const values = validateForm($('#fea-form'));
  if (!values) return;
  jobStarted('starting thermal job…');
  try {
    const s = await runJob(`/api/thermal/solve?${query(values)}`, jobProgress);
    if (s.state === 'cancelled') { jobFinished('thermal job cancelled; the previous results are unchanged'); return; }
    lastValues = values;
    render(s.result);
    jobFinished(`thermal job done in ${(s.elapsed_ms / 1000).toFixed(2)} s`);
  } catch (e) {
    jobFinished('thermal job failed');
    showError(e, 'thermal job');
  }
}

function render(r) {
  const finest = r.meshes[r.meshes.length - 1];
  const balance = r.heat_out_left_W + r.heat_out_right_W;
  const order = finest.observed_order;
  setKpis($('#fea-kpis'), [
    { label: 'peak temperature', value: fmt(r.peak_T_C, 5), unit: '°C', foot: `at x = ${fmt(r.peak_x_m, 4)} m (exact)` },
    { label: 'heat out of the ends', value: `${fmt(r.heat_out_left_W, 3)} + ${fmt(r.heat_out_right_W, 3)}`, unit: 'W',
      foot: `= ${fmt(balance, 4)} W, generated qAL = ${fmt(r.heat_generated_W, 4)} W`, ok: Math.abs(balance - r.heat_generated_W) < 1e-9 * Math.max(1, Math.abs(r.heat_generated_W)) },
    { label: `max midpoint error, N = ${finest.elements}`, value: fmt(finest.max_midpoint_error_C, 4), unit: '°C', foot: `theory qh²/8k = ${fmt(finest.theory_midpoint_error_C, 4)} °C` },
    { label: 'observed order', value: order === null ? '–' : order.toFixed(3), foot: 'second order expected (2.000)', ok: order !== null && Math.abs(order - 2) < 0.01 },
  ]);

  const colors = (i) => PALETTE[i % PALETTE.length];
  tempPlot.setSeries([
    ...r.meshes.map((m, i) => ({ name: `N = ${m.elements}`, x: m.x_m, y: m.T_C, color: colors(i), markers: m.elements <= 20 })),
    { name: 'exact', x: r.exact.x_m, y: r.exact.T_C, color: '#6b7180', dash: true },
  ], { keepZoom: true });
  errPlot.setSeries(r.meshes.map((m, i) => ({
    name: `N = ${m.elements}`, x: m.err_x_m, y: m.err_abs_C, color: colors(i), width: 1.6,
  })), { keepZoom: true });
  const h = r.meshes.map((m) => m.h_m);
  convPlot.setSeries([
    { name: 'midpoint', x: h, y: r.meshes.map((m) => m.max_midpoint_error_C), markers: true, color: '#e5484d' },
    { name: 'qh²/8k', x: h, y: r.meshes.map((m) => m.theory_midpoint_error_C), dash: true, color: '#6b7180' },
    { name: 'nodal', x: h, y: r.meshes.map((m) => Math.max(m.max_nodal_error_C, 1e-16)), markers: true, noLine: true, color: '#2563eb' },
  ]);

  const body = $('#fea-table tbody');
  body.replaceChildren();
  for (const m of r.meshes) {
    const tr = document.createElement('tr');
    cell(tr, String(m.elements));
    cell(tr, fmt(m.h_m, 4));
    cell(tr, fmt(m.max_midpoint_error_C, 8));
    cell(tr, fmt(m.theory_midpoint_error_C, 8));
    cell(tr, fmt(m.worst_midpoint_x_m, 4));
    cell(tr, fmt(m.max_nodal_error_C, 2));
    cell(tr, m.observed_order === null ? '–' : m.observed_order.toFixed(4), m.observed_order !== null && Math.abs(m.observed_order - 2) < 0.01 ? 'ok' : '');
    body.appendChild(tr);
  }
}
