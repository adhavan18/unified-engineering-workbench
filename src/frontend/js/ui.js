// shared ui pieces: status bar, job progress, toasts, form validation.

import { cancelJob } from './api.js';

const $ = (sel) => document.querySelector(sel);

export function setStatus(text) {
  $('#status-text').textContent = text;
}

export function toast(title, { hint = '', kind = 'error', ms = 7000 } = {}) {
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

// ---- form validation --------------------------------------------------------
// rules mirror the backend checks so mistakes are caught before a request;
// the backend still validates everything.

const RULES = {
  pos: (v) => (Number.isFinite(+v) && v.trim() !== '' && +v > 0 ? '' : 'enter a positive number'),
  num: (v) => (Number.isFinite(+v) && v.trim() !== '' ? '' : 'enter a number'),
  temp: (v) => (!Number.isFinite(+v) || v.trim() === '' ? 'enter a number' : +v < -273.15 ? 'below absolute zero' : ''),
  int: (v) => (/^\s*\d+\s*$/.test(v) && +v >= 1 ? '' : 'enter a whole number ≥ 1'),
  int0: (v) => (/^\s*\d+\s*$/.test(v) ? '' : 'enter a whole number ≥ 0'),
  intlist: (v) => {
    const parts = v.split(',').map((s) => s.trim()).filter(Boolean);
    if (!parts.length) return 'list at least one value, e.g. 10,20,40';
    return parts.every((p) => /^\d+$/.test(p) && +p >= 1) ? '' : 'comma-separated whole numbers ≥ 1';
  },
};

export function validateForm(form) {
  let ok = true;
  const values = {};
  for (const input of form.querySelectorAll('input[data-rule]')) {
    const msg = RULES[input.dataset.rule](input.value);
    const label = input.closest('.field');
    label.querySelector('.field-error')?.remove();
    input.setAttribute('aria-invalid', msg ? 'true' : 'false');
    if (msg) {
      ok = false;
      const e = document.createElement('span');
      e.className = 'field-error';
      e.textContent = msg;
      label.appendChild(e);
    }
    values[input.name] = input.value.trim().replace(/\s+/g, '');
  }
  if (!ok) form.querySelector('[aria-invalid="true"]')?.focus();
  return ok ? values : null;
}

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

export function fmt(v, digits = 4) {
  if (v === null || v === undefined || Number.isNaN(v)) return '–';
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a < 1e-3 || a >= 1e6) return v.toExponential(digits - 1);
  return Number(v.toPrecision(digits)).toString();
}

export function cell(tr, text, cls) {
  const td = document.createElement('td');
  td.textContent = text;
  if (cls) td.className = cls;
  tr.appendChild(td);
  return td;
}
