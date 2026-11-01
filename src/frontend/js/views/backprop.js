// backprop view: values come from the c++ implementation (hand-derived chain
// rule, no autodiff). sliders recompute live; the scrubber walks the training.

import { download, getJson, query } from '../api.js';
import { bindSliders, cell, debounce, fmt, setField, setKpis, showError, validateForm } from '../ui.js';
import { LinePlot } from '../plot.js';

const $ = (s) => document.querySelector(s);
const svgNS = 'http://www.w3.org/2000/svg';
const BRIEF = { x: '1', y: '2', w: '0.5', b: '0', v: '1', c: '0', lr: '0.1' };
export const plots = {};
let lossPlot, paramPlot, data = null, values = null, timer = null, loaded = false;

export function initBackprop() {
  lossPlot = new LinePlot($('#bp-loss'), { xLabel: 'step', yLabel: 'loss J', logY: true });
  paramPlot = new LinePlot($('#bp-params'), { xLabel: 'step', yLabel: 'value' });
  Object.assign(plots, { 'bp-loss': lossPlot, 'bp-params': paramPlot });
  const form = $('#bp-form');
  form.addEventListener('submit', (e) => e.preventDefault());
  bindSliders(form, debounce(load, 50));
  $('#bp-reset').addEventListener('click', () => { for (const [k, v] of Object.entries(BRIEF)) setField(form, k, v); load(); });
  $('#bp-step').addEventListener('input', (e) => { stop(); showStep(+e.target.value); });
  $('#bp-play').addEventListener('click', () => (timer ? stop() : play()));
  for (const btn of document.querySelectorAll('[data-bp-export]')) {
    btn.addEventListener('click', () => download(`/api/backprop/export?what=${btn.dataset.bpExport}&${query(values || BRIEF)}`));
  }
  buildGraph();
}

export function showBackprop() { if (!loaded) load(); }

async function load() {
  const v = validateForm($('#bp-form'));
  if (!v) return;
  try {
    data = await getJson(`/api/backprop?${query(v)}`);
    values = v;
    loaded = true;
    render();
  } catch (e) {
    showError(e, 'backprop');
  }
}

function play() {
  $('#bp-play').textContent = '❚❚ pause';
  let s = +$('#bp-step').value;
  if (s >= 101) s = 0;
  timer = setInterval(() => {
    s += 1;
    if (s > 101) { stop(); return; }
    $('#bp-step').value = s;
    showStep(s);
  }, 70);
}

function stop() {
  clearInterval(timer);
  timer = null;
  $('#bp-play').textContent = '▶ play';
}

function render() {
  const r = data, f = r.forward;
  const maxErr = Math.max(...r.gradients.map((g) => g.abs_error));
  setKpis($('#bp-kpis'), [
    { label: 'loss at start', value: fmt(f.loss, 6), foot: `ŷ = ${fmt(f.y_hat, 6)} vs y = ${values.y}` },
    { label: 'after the handwritten step', value: fmt(r.loss_after_first_step, 6), foot: `${((1 - r.loss_after_first_step / f.loss) * 100).toFixed(1)} % lower` },
    { label: 'gradient check', value: `${r.gradients.filter((g) => g.pass).length} / 4`, unit: 'pass', foot: `max |analytic − numeric| = ${maxErr.toExponential(1)}`, ok: r.gradient_check_ok },
    { label: 'loss after 101 steps', value: fmt(r.losses[r.losses.length - 1], 3), foot: 'gradient descent, same η' },
  ]);

  const body = $('#bp-table tbody');
  body.replaceChildren();
  for (const g of r.gradients) {
    const tr = document.createElement('tr');
    cell(tr, g.parameter);
    cell(tr, String(g.before));
    cell(tr, g.analytic.toPrecision(10));
    cell(tr, g.central_difference.toPrecision(10));
    cell(tr, g.abs_error.toExponential(2));
    cell(tr, g.pass ? 'pass' : 'FAIL', g.pass ? 'ok' : 'err');
    cell(tr, g.after.toPrecision(10));
    body.appendChild(tr);
  }
  const e = f.y_hat - +values.y;
  $('#bp-explain').textContent = e < 0
    ? `ŷ = ${fmt(f.y_hat, 4)} is below the target ${values.y}, so the error ŷ − y is negative. every parameter that raises ŷ (here: the ones with a negative gradient) is pushed up by −η·∂J/∂θ. near the optimum the loss is locally quadratic, so it falls roughly geometrically: a straight line on the log axis.`
    : `ŷ = ${fmt(f.y_hat, 4)} is above the target ${values.y}, so the error is positive and descent pushes ŷ down. near the optimum the loss is locally quadratic, so it falls roughly geometrically: a straight line on the log axis.`;

  const steps = r.losses.map((_, i) => i);
  lossPlot.setSeries([{ name: 'J', x: steps, y: r.losses.map((v) => Math.max(v, 1e-30)), color: '#e5484d' }]);
  const t = r.trajectory;
  paramPlot.setSeries(['w', 'b', 'v', 'c'].map((k, i) => ({ name: k, x: steps, y: t[k], color: ['#2563eb', '#0894b3', '#8e4ec6', '#f76b15'][i] })));
  showStep(+$('#bp-step').value);
}

// ---------------- computation graph ----------------
const NODES = {
  x: { at: [70, 50], title: 'x', kind: 'input' },
  w: { at: [70, 130], title: 'w', kind: 'param' },
  b: { at: [70, 210], title: 'b', kind: 'param' },
  z: { at: [270, 130], title: 'z = wx + b' },
  h: { at: [470, 130], title: 'h = tanh(z)' },
  v: { at: [470, 40], title: 'v', kind: 'param' },
  c: { at: [470, 220], title: 'c', kind: 'param' },
  yhat: { at: [670, 130], title: 'ŷ = vh + c' },
  y: { at: [830, 40], title: 'target y', kind: 'input' },
  J: { at: [830, 130], title: 'J = ½(ŷ − y)²', kind: 'loss' },
};
const EDGES = [['x', 'z'], ['w', 'z'], ['b', 'z'], ['z', 'h'], ['h', 'yhat'], ['v', 'yhat'], ['c', 'yhat'], ['yhat', 'J'], ['y', 'J']];

function buildGraph() {
  const svg = $('#bp-graph');
  svg.replaceChildren();
  const W = 128, H = 54;
  for (const [a, b] of EDGES) {
    const [x1, y1] = NODES[a].at, [x2, y2] = NODES[b].at;
    const p = document.createElementNS(svgNS, 'path');
    const sx = x1 + W / 2, ex = x2 - W / 2;
    p.setAttribute('d', a === 'yhat' || a === 'y' ? `M${x1 + (a === 'y' ? 0 : W / 2)} ${a === 'y' ? y1 + H / 2 : y1} ${a === 'y' ? `L${x2} ${y2 - H / 2}` : `L${ex} ${y2}`}`
      : `M${sx} ${y1} C ${(sx + ex) / 2} ${y1}, ${(sx + ex) / 2} ${y2}, ${ex} ${y2}`);
    p.setAttribute('class', 'edge');
    svg.appendChild(p);
  }
  for (const [id, n] of Object.entries(NODES)) {
    const g = document.createElementNS(svgNS, 'g');
    g.setAttribute('class', `gnode ${n.kind || ''}`);
    const r = document.createElementNS(svgNS, 'rect');
    r.setAttribute('x', n.at[0] - W / 2); r.setAttribute('y', n.at[1] - H / 2); r.setAttribute('width', W); r.setAttribute('height', H);
    const t = document.createElementNS(svgNS, 'text');
    t.setAttribute('x', n.at[0]); t.setAttribute('y', n.at[1] - 8); t.setAttribute('text-anchor', 'middle');
    t.textContent = n.title;
    const v = document.createElementNS(svgNS, 'text');
    v.setAttribute('x', n.at[0]); v.setAttribute('y', n.at[1] + 9); v.setAttribute('text-anchor', 'middle'); v.setAttribute('class', 'val');
    v.id = `gv-${id}`;
    const gr = document.createElementNS(svgNS, 'text');
    gr.setAttribute('x', n.at[0]); gr.setAttribute('y', n.at[1] + H / 2 + 15); gr.setAttribute('text-anchor', 'middle'); gr.setAttribute('class', 'grad');
    gr.id = `gg-${id}`;
    g.append(r, t, v, gr);
    svg.appendChild(g);
  }
}

function showStep(s) {
  if (!data?.trajectory) return;
  const t = data.trajectory, i = Math.min(s, t.w.length - 1);
  const y = +values.y, x = +values.x;
  const yhat = t.y_hat[i], e = yhat - y, h = t.h[i];
  const set = (id, text) => { $(`#gv-${id}`).textContent = text; };
  const grad = (id, text) => { $(`#gg-${id}`).textContent = text; };
  const f6 = (v) => fmt(v, 6);
  set('x', f6(x)); set('y', f6(y));
  set('w', f6(t.w[i])); set('b', f6(t.b[i])); set('v', f6(t.v[i])); set('c', f6(t.c[i]));
  set('z', f6(t.z[i])); set('h', f6(h)); set('yhat', f6(yhat)); set('J', f6(0.5 * e * e));
  grad('w', `∂J/∂w ${f6(t.dw[i])}`); grad('b', `∂J/∂b ${f6(t.db[i])}`);
  grad('v', `∂J/∂v ${f6(t.dv[i])}`); grad('c', `∂J/∂c ${f6(t.dc[i])}`);
  grad('z', `∂J/∂z ${f6(t.db[i])}`); grad('h', `∂J/∂h ${f6(e * t.v[i])}`); grad('yhat', `∂J/∂ŷ ${f6(e)}`);
  grad('x', ''); grad('y', ''); grad('J', '');
  $('#bp-step-label').textContent = String(i);
  $('#bp-step-num').textContent = `step ${i} / ${t.w.length - 1}`;
  lossPlot.setMarker(i);
  paramPlot.setMarker(i);
}
