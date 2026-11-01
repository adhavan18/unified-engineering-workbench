// hardware view: simulated mcu / fpga / jtag adapters from the c++ registry,
// with a live topology, streamed telemetry + sparklines and fault injection.

import { getJson, postJson } from '../api.js';
import { setFacts, setStatus, showError } from '../ui.js';
import { sparkline } from '../plot.js';

const $ = (s) => document.querySelector(s);
const KIND = { microcontroller: 'microcontroller board', fpga: 'fpga board', jtag_adapter: 'jtag debug adapter' };
const devices = new Map();  // id -> { info, card, streaming, timer, history: Map(name -> number[]) }

const svgNS = 'http://www.w3.org/2000/svg';
const POS = { host: [450, 40], hub: [450, 112], mcu0: [150, 205], fpga0: [450, 205], jtag0: [750, 205] };
const LABEL = { host: 'host pc · workbench', hub: 'powered usb hub', mcu0: 'mcu0 · stm32f4-class', fpga0: 'fpga0 · artix-7-class', jtag0: 'jtag0 · ft2232h-class' };

export function initHardware() {
  $('#hw-discover').addEventListener('click', discover);
  $('#hw-clear').addEventListener('click', () => { $('#hw-log').textContent = ''; });
  drawTopology();
}

function log(line, level = '') {
  const el = $('#hw-log');
  const span = document.createElement('span');
  if (level) span.className = level;
  span.textContent = `${new Date().toLocaleTimeString()}  ${line}\n`;
  el.appendChild(span);
  el.scrollTop = el.scrollHeight;
}

function drawTopology() {
  const svg = $('#topology');
  svg.replaceChildren();
  const line = (a, b, id, extra = '') => {
    const p = document.createElementNS(svgNS, 'path');
    const [x1, y1] = POS[a], [x2, y2] = POS[b];
    p.setAttribute('d', `M${x1} ${y1 + 18} C ${x1} ${(y1 + y2) / 2}, ${x2} ${(y1 + y2) / 2}, ${x2} ${y2 - 18}`);
    p.setAttribute('class', `link ${extra}`);
    if (id) p.id = id;
    svg.appendChild(p);
  };
  line('host', 'hub', 'link-hub');
  for (const d of ['mcu0', 'fpga0', 'jtag0']) line('hub', d, `link-${d}`);
  // the jtag ribbon goes adapter -> fpga test access port
  const tap = document.createElementNS(svgNS, 'path');
  tap.setAttribute('d', `M${POS.jtag0[0] - 95} ${POS.jtag0[1]} L${POS.fpga0[0] + 95} ${POS.fpga0[1]}`);
  tap.setAttribute('class', 'link');
  tap.setAttribute('stroke-dasharray', '3 4');
  tap.id = 'link-tap';
  svg.appendChild(tap);
  const tapLabel = document.createElementNS(svgNS, 'text');
  tapLabel.setAttribute('x', (POS.jtag0[0] + POS.fpga0[0]) / 2);
  tapLabel.setAttribute('y', POS.fpga0[1] - 8);
  tapLabel.setAttribute('text-anchor', 'middle');
  tapLabel.setAttribute('style', 'font: 600 11px system-ui; fill: var(--sim)');
  tapLabel.textContent = 'jtag → tap';
  svg.appendChild(tapLabel);

  for (const [id, [x, y]] of Object.entries(POS)) {
    const g = document.createElementNS(svgNS, 'g');
    g.setAttribute('class', 'node');
    g.id = `topo-${id}`;
    const r = document.createElementNS(svgNS, 'rect');
    const w = 190, h = 36;
    r.setAttribute('x', x - w / 2); r.setAttribute('y', y - h / 2); r.setAttribute('width', w); r.setAttribute('height', h); r.setAttribute('rx', 10);
    r.setAttribute('style', 'fill: var(--surface-2); stroke: var(--border-strong); stroke-width: 1.5');
    const t = document.createElementNS(svgNS, 'text');
    t.setAttribute('x', x); t.setAttribute('y', y + 4); t.setAttribute('text-anchor', 'middle');
    t.setAttribute('style', 'font: 600 12.5px system-ui; fill: var(--text)');
    t.textContent = LABEL[id];
    g.append(r, t);
    if (devices.has(id) || id.endsWith('0')) {
      g.style.cursor = 'pointer';
      g.addEventListener('click', () => $(`#dev-${id}`)?.scrollIntoView({ behavior: 'smooth', block: 'center' }));
    }
    svg.appendChild(g);
  }
}

function paintTopology() {
  for (const [id, d] of devices) {
    const rect = $(`#topo-${id} rect`);
    const st = d.state;
    const fill = st === 'connected' ? 'var(--ok-soft)' : st === 'faulted' ? 'var(--err-soft)' : 'var(--surface-2)';
    const stroke = st === 'connected' ? 'var(--ok)' : st === 'faulted' ? 'var(--err)' : 'var(--border-strong)';
    rect?.setAttribute('style', `fill: ${fill}; stroke: ${stroke}; stroke-width: 1.5`);
    $(`#link-${id}`)?.setAttribute('class', `link${d.streaming && st === 'connected' ? ' live' : ''}`);
  }
  const jtag = devices.get('jtag0');
  $('#link-tap')?.setAttribute('class', `link${jtag?.streaming && jtag.state === 'connected' ? ' live' : ''}`);
  const anyLive = [...devices.values()].some((d) => d.streaming && d.state === 'connected');
  $('#link-hub')?.setAttribute('class', `link${anyLive ? ' live' : ''}`);
  const host = $('#topo-host rect');
  host?.setAttribute('style', `fill: var(--accent-soft); stroke: var(--accent); stroke-width: 1.5`);
}

export async function discover() {
  try {
    const r = await getJson('/api/hardware/devices');
    for (const d of devices.values()) clearInterval(d.timer);
    devices.clear();
    const grid = $('#hw-devices');
    grid.replaceChildren(...r.devices.map(renderDevice));
    paintTopology();
    log(`discovery (simulated): ${r.devices.map((d) => `${d.id} [${d.kind}]`).join(', ')}`);
    setStatus(`${r.devices.length} simulated devices discovered`);
  } catch (e) {
    showError(e, 'discovery');
  }
}

function renderDevice(d) {
  const card = document.createElement('article');
  card.className = 'card device';
  card.id = `dev-${d.id}`;
  card.innerHTML = `
    <div class="row between"><div><div class="kind"></div><h3></h3></div><span class="badge badge-sim">simulated</span></div>
    <div class="states"><span data-s="disconnected">disconnected</span>→<span data-s="connected">connected</span>→<span data-s="faulted">faulted</span></div>
    <dl class="facts info"></dl>
    <details class="more"><summary>capabilities</summary><div><ul class="caps"></ul></div></details>
    <div class="row wrap">
      <button class="btn btn-small btn-primary" data-act="connect">connect</button>
      <button class="btn btn-small" data-act="disconnect">disconnect</button>
      <button class="btn btn-small" data-act="telemetry">read once</button>
      <label class="switch small"><input type="checkbox" data-stream> stream</label>
    </div>
    <div class="row wrap">
      <button class="btn btn-small btn-danger" data-fault="timeout_once">inject timeout</button>
      <button class="btn btn-small btn-ghost" data-fault="timeout_always">keep timing out</button>
      <button class="btn btn-small btn-ghost" data-fault="none">clear fault</button>
    </div>
    <div class="notice notice-err recover" hidden></div>
    <div class="telemetry"></div>`;
  card.querySelector('.kind').textContent = KIND[d.kind] || d.kind;
  card.querySelector('h3').textContent = `${d.id}`;
  setFacts(card.querySelector('.info'), [
    ['model', d.model], ['host link', d.host_link], ['i/o level', d.io_voltage],
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
  const entry = { info: d, card, state: d.state, streaming: false, timer: null, history: new Map(), busy: false };
  devices.set(d.id, entry);
  for (const btn of card.querySelectorAll('[data-act]')) btn.addEventListener('click', () => act(d.id, btn.dataset.act));
  for (const btn of card.querySelectorAll('[data-fault]')) btn.addEventListener('click', () => act(d.id, 'fault', btn.dataset.fault));
  card.querySelector('[data-stream]').addEventListener('change', (e) => setStreaming(d.id, e.target.checked));
  updateState(entry, d);
  return card;
}

function setStreaming(id, on) {
  const d = devices.get(id);
  d.streaming = on;
  clearInterval(d.timer);
  if (on) d.timer = setInterval(() => { if (!d.busy && d.state === 'connected') act(id, 'telemetry', null, true); }, 600);
  paintTopology();
}

function updateState(entry, dev) {
  entry.state = dev.state;
  const card = entry.card;
  card.classList.remove('state-connected', 'state-faulted', 'state-disconnected');
  card.classList.add(`state-${dev.state}`);
  for (const s of card.querySelectorAll('.states span')) s.className = s.dataset.s === dev.state ? `on ${dev.state}` : '';
  card.querySelector('[data-act="connect"]').textContent = dev.state === 'faulted' ? 'retry connect' : 'connect';
  paintTopology();
}

async function act(id, action, mode, quiet = false) {
  const d = devices.get(id);
  if (!d || d.busy) return;
  d.busy = true;
  const buttons = d.card.querySelectorAll('button');
  if (!quiet) for (const b of buttons) b.disabled = true;
  try {
    const r = await postJson(`/api/hardware/${id}/${action}${mode ? `?mode=${mode}` : ''}`);
    const res = r.result;
    if (!quiet || !res.ok) log(`${id} ${action}${mode ? ` ${mode}` : ''}: ${res.code} (${res.elapsed_ms.toFixed(0)} ms) ${res.message}`, res.ok ? '' : 'warn');
    const recover = d.card.querySelector('.recover');
    recover.hidden = res.ok;
    if (!res.ok) recover.textContent = `${res.code}: ${res.message}`;
    if (r.telemetry) renderTelemetry(d, r.telemetry);
    if (action === 'disconnect') { d.card.querySelector('.telemetry').replaceChildren(); d.history.clear(); }
    updateState(d, r.device);
  } catch (e) {
    showError(e, id);
  } finally {
    d.busy = false;
    for (const b of buttons) b.disabled = false;
  }
}

function renderTelemetry(d, t) {
  const box = d.card.querySelector('.telemetry');
  const rows = [['frame', `#${t.sequence} (simulated)`, null]];
  for (const f of t.fields) {
    const v = parseFloat(f.value);
    if (Number.isFinite(v) && /^[-\d.]+$/.test(f.value)) {
      const h = d.history.get(f.name) || [];
      h.push(v);
      if (h.length > 40) h.shift();
      d.history.set(f.name, h);
      rows.push([f.name, `${f.value}${f.unit ? ' ' + f.unit : ''}`, h]);
    } else {
      rows.push([f.name, `${f.value}${f.unit ? ' ' + f.unit : ''}`, null]);
    }
  }
  box.replaceChildren();
  for (const [k, v, hist] of rows) {
    const a = document.createElement('span'); a.className = 'k'; a.textContent = k;
    const b = document.createElement('span'); b.textContent = v;
    const c = document.createElement('canvas');
    box.append(a, b, c);
    if (hist) requestAnimationFrame(() => sparkline(c, hist));
  }
}

export function stopStreams() {
  for (const d of devices.values()) clearInterval(d.timer);
}
