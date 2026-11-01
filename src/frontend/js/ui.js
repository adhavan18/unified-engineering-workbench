// shared ui pieces: status bar, job progress, toasts, kpi tiles, slider fields,
// form validation and number formatting.

import { cancelJob } from './api.js';

const $ = (sel) => document.querySelector(sel);

export function setStatus(text) {
  $('#status-text').textContent = text;
}

export function toast(title, { hint = '', kind = 'error', ms = 6500 } = {}) {
  const el = document.createElement('div');
  el.className = `toast ${kind === 'error' ? '' : kind}`;
  const b = document.createElement('b');
  b.textContent = title;
  el.appendChild(b);
  if (hint) {
    const h = document.createElement('div');
    h.className = 'hint';
    h.textContent = hint;
    el.appendChild(h);
  }
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), ms);
}

export function showError(err, context = '') {
  const where = err.line ? ` (line ${err.line})` : '';
  toast(`${context ? context + ': ' : ''}${err.message}${where}`, { hint: err.hint });
  setStatus(`error: ${err.message}`);
}

// ---- job progress in the status bar ---------------------------------------

let currentJob = null;

export function jobStarted(label) {
  currentJob = null;
  $('#job').hidden = false;
  $('#status-text').hidden = true;
  $('#job-label').textContent = label;
  $('#job-fill').style.width = '0%';
  $('#job-cancel').disabled = false;
}

export function jobProgress(status) {
  currentJob = status.id;
  $('#job-label').textContent = status.message;
  $('#job-fill').style.width = `${Math.round(status.progress * 100)}%`;
}

export function jobFinished(text) {
  currentJob = null;
  $('#job').hidden = true;
  $('#status-text').hidden = false;
  setStatus(text);
}

export function initJobCancel() {
  $('#job-cancel').addEventListener('click', async () => {
    if (!currentJob) return;
    $('#job-cancel').disabled = true;
    $('#job-label').textContent = 'cancelling…';
    try { await cancelJob(currentJob); } catch (e) { showError(e); }
  });
}

// ---- validation -------------------------------------------------------------
// mirrors the backend checks so mistakes are caught instantly; the backend
// still validates everything it receives.

const RULES = {
  pos: (v) => (v.trim() !== '' && Number.isFinite(+v) && +v > 0 ? '' : 'enter a positive number'),
  num: (v) => (v.trim() !== '' && Number.isFinite(+v) ? '' : 'enter a number'),
  temp: (v) => (v.trim() === '' || !Number.isFinite(+v) ? 'enter a number' : +v < -273.15 ? 'below absolute zero (−273.15 °C)' : ''),
  int: (v) => (/^\s*\d+\s*$/.test(v) && +v >= 1 ? '' : 'enter a whole number ≥ 1'),
  int0: (v) => (/^\s*\d+\s*$/.test(v) ? '' : 'enter a whole number ≥ 0'),
  intlist: (v) => {
    const parts = v.split(',').map((s) => s.trim()).filter(Boolean);
    if (!parts.length) return 'pick at least one';
    return parts.every((p) => /^\d+$/.test(p) && +p >= 1) ? '' : 'comma-separated whole numbers ≥ 1';
  },
};

export function validateForm(form) {
  let ok = true;
  const values = {};
  for (const input of form.querySelectorAll('input[data-rule]')) {
    const msg = RULES[input.dataset.rule](input.value);
    const field = input.closest('.field') || input.parentElement;
    field.querySelector('.field-error')?.remove();
    input.setAttribute('aria-invalid', msg ? 'true' : 'false');
    if (msg) {
      ok = false;
      const e = document.createElement('span');
      e.className = 'field-error';
      e.textContent = msg;
      field.appendChild(e);
    }
    values[input.name] = input.value.trim().replace(/\s+/g, '');
  }
  return ok ? values : null;
}

// wires every [data-slider] field: the range and the text box mirror each
// other, and onChange fires continuously while dragging.
export function bindSliders(root, onChange) {
  for (const field of root.querySelectorAll('[data-slider]')) {
    const range = field.querySelector('input[type="range"]');
    const text = field.querySelector('input[type="text"]');
    range.min = field.dataset.min;
    range.max = field.dataset.max;
    range.step = field.dataset.step;
    range.value = text.value;
    range.addEventListener('input', () => { text.value = range.value; onChange(); });
    text.addEventListener('input', () => {
      if (Number.isFinite(+text.value) && text.value.trim() !== '') range.value = text.value;
      onChange();
    });
  }
}

export function setField(root, name, value) {
  const text = root.querySelector(`[name="${name}"]`);
  text.value = value;
  const range = text.closest('[data-slider]')?.querySelector('input[type="range"]');
  if (range) range.value = value;
}

export function debounce(fn, ms) {
  let t = null;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
}

// ---- display helpers ----------------------------------------------------------

export function setFacts(dl, pairs) {
  dl.replaceChildren();
  for (const [k, v] of pairs) {
    const dt = document.createElement('dt');
    dt.textContent = k;
    const dd = document.createElement('dd');
    dd.textContent = v;
    dl.append(dt, dd);
  }
}

// tiles: [{ label, value, unit?, foot?, ok? }]
export function setKpis(el, tiles) {
  el.replaceChildren(...tiles.map((t) => {
    const d = document.createElement('div');
    d.className = `kpi${t.ok ? ' ok' : ''}`;
    d.innerHTML = '<div class="label"></div><div class="value"></div><div class="foot"></div>';
    d.querySelector('.label').textContent = t.label;
    const v = d.querySelector('.value');
    v.textContent = t.value;
    if (t.unit) { const s = document.createElement('small'); s.textContent = t.unit; v.appendChild(s); }
    d.querySelector('.foot').textContent = t.foot || '';
    return d;
  }));
}

export function fmt(v, digits = 4) {
  if (v === null || v === undefined || Number.isNaN(v)) return '–';
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a < 1e-3 || a >= 1e6) return v.toExponential(Math.max(0, digits - 1));
  return Number(v.toPrecision(digits)).toString();
}

export function cell(tr, text, cls) {
  const td = document.createElement('td');
  td.textContent = text;
  if (cls) td.className = cls;
  tr.appendChild(td);
  return td;
}

export function chips(container, values, selected, onToggle, { suffix = '' } = {}) {
  container.replaceChildren(...values.map((v) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chip';
    b.textContent = `${v}${suffix}`;
    b.setAttribute('aria-pressed', String(selected.includes(v)));
    b.addEventListener('click', () => {
      b.setAttribute('aria-pressed', String(b.getAttribute('aria-pressed') !== 'true'));
      onToggle(values.filter((_, i) => container.children[i].getAttribute('aria-pressed') === 'true'));
    });
    return b;
  }));
}
