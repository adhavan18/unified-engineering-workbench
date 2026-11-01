// backprop view: values come from the c++ implementation (no autodiff).

import { download, getJson } from '../api.js';
import { cell, setFacts, showError } from '../ui.js';
import { LinePlot } from '../plot.js';

const $ = (s) => document.querySelector(s);
let plot, loaded = false;
export const plots = {};

export function initBackprop() {
  plot = new LinePlot($('#bp-plot'), { xLabel: 'gradient-descent step', yLabel: 'loss J', logY: true });
  plots['bp-plot'] = plot;
  for (const btn of document.querySelectorAll('[data-bp-export]')) {
    btn.addEventListener('click', () => download(`/api/backprop/export?what=${btn.dataset.bpExport}`));
  }
}

export async function showBackprop() {
  if (loaded) return;
  try {
    const r = await getJson('/api/backprop');
    loaded = true;
    const f = r.forward;
    setFacts($('#bp-forward'), [
      ['z = wx + b', f.z.toPrecision(12)],
      ['h = tanh(z)', f.h.toPrecision(12)],
      ['ŷ = vh + c', f.y_hat.toPrecision(12)],
      ['J = ½(ŷ − y)²', f.loss.toPrecision(12)],
      ['J after 1 step', r.loss_after_first_step.toPrecision(12)],
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
    const steps = r.losses.map((_, i) => i);
    plot.setSeries([{ name: 'loss J', x: steps, y: r.losses, color: '#0a66d8' }]);
  } catch (e) {
    showError(e, 'backprop');
  }
}
