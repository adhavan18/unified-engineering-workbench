// hardware view: simulated mcu / fpga / jtag adapters from the c++ registry.

import { getJson, postJson } from '../api.js';
import { setFacts, showError, setStatus } from '../ui.js';

const $ = (s) => document.querySelector(s);
const KIND_LABEL = { microcontroller: 'microcontroller', fpga: 'fpga board', jtag_adapter: 'jtag debug adapter' };
const STATE_CLASS = { connected: 'pill-ok', disconnected: 'pill-muted', faulted: 'pill-err' };

export function initHardware() {
  $('#hw-discover').addEventListener('click', discover);
}

function log(line) {
  const el = $('#hw-log');
  el.textContent += `${new Date().toLocaleTimeString()}  ${line}\n`;
  el.scrollTop = el.scrollHeight;
}

async function discover() {
  try {
    const r = await getJson('/api/hardware/devices');
    const grid = $('#hw-devices');
    grid.replaceChildren(...r.devices.map(renderDevice));
    log(`discovery (simulated): found ${r.devices.map((d) => d.id).join(', ')}`);
    setStatus('3 simulated devices discovered');
  } catch (e) {
    showError(e, 'discovery');
  }
}

function renderDevice(d) {
  const card = document.createElement('article');
  card.className = 'card device';
  card.id = `dev-${d.id}`;
  card.innerHTML = `
    <div class="card-head">
      <div><div class="kind"></div><h3></h3></div>
      <span class="badge badge-sim">simulated</span>
    </div>
    <div class="row gap-s"><span class="pill state"></span><span class="muted small fault"></span></div>
    <dl class="facts info"></dl>
    <ul class="caps"></ul>
    <div class="actions">
      <button class="btn btn-small btn-primary" data-act="connect">connect</button>
      <button class="btn btn-small" data-act="telemetry">read telemetry</button>
      <button class="btn btn-small" data-act="disconnect">disconnect</button>
      <label class="field-inline small">fault
        <select data-fault>
          <option value="none">none</option>
          <option value="timeout_once">timeout once (recoverable)</option>
          <option value="timeout_always">timeout always</option>
        </select>
      </label>
    </div>
    <div class="notice notice-warn recover" hidden></div>
    <dl class="facts telemetry"></dl>`;
  card.querySelector('.kind').textContent = KIND_LABEL[d.kind] || d.kind;
  card.querySelector('h3').textContent = d.id;
  setFacts(card.querySelector('.info'), [
    ['model', d.model],
    ['host link', d.host_link],
    ['i/o level', d.io_voltage],
    ['runs workloads', d.can_run_workloads ? 'yes' : 'no: debug interface only'],
  ]);
  const caps = card.querySelector('.caps');
  for (const c of d.capabilities) {
    const li = document.createElement('li');
    const b = document.createElement('b');
    b.textContent = c.name;
    li.append(b, `: ${c.detail}`);
    caps.appendChild(li);
  }
  for (const btn of card.querySelectorAll('[data-act]')) btn.addEventListener('click', () => act(d.id, btn.dataset.act, card));
  card.querySelector('[data-fault]').addEventListener('change', (e) => act(d.id, 'fault', card, e.target.value));
  updateState(card, d);
  return card;
}

function updateState(card, d) {
  const pill = card.querySelector('.state');
  pill.className = `pill state ${STATE_CLASS[d.state] || ''}`;
  pill.textContent = d.state;
  card.querySelector('.fault').textContent = d.fault === 'none' ? '' : `fault injection: ${d.fault.replace('_', ' ')}`;
  card.querySelector('[data-fault]').value = d.fault;
  card.querySelector('[data-act="connect"]').textContent = d.state === 'faulted' ? 'retry connect' : 'connect';
}

async function act(id, action, card, mode) {
  card.classList.add('busy');
  for (const b of card.querySelectorAll('button')) b.disabled = true;
  try {
    const url = `/api/hardware/${id}/${action}${mode ? `?mode=${mode}` : ''}`;
    const r = await postJson(url);
    const res = r.result;
    log(`${id} ${action}: ${res.code} (${res.elapsed_ms.toFixed(0)} ms) ${res.message}`);
    const recover = card.querySelector('.recover');
    recover.hidden = res.ok;
    if (!res.ok) recover.textContent = `${res.code}: ${res.message}`;
    if (r.telemetry) {
      setFacts(card.querySelector('.telemetry'), [
        ['frame', `#${r.telemetry.sequence} (simulated)`],
        ...r.telemetry.fields.map((f) => [f.name, `${f.value}${f.unit ? ' ' + f.unit : ''}`]),
      ]);
    } else if (action === 'disconnect') {
      card.querySelector('.telemetry').replaceChildren();
    }
    updateState(card, r.device);
  } catch (e) {
    showError(e, id);
  } finally {
    card.classList.remove('busy');
    for (const b of card.querySelectorAll('button')) b.disabled = false;
  }
}
