// app shell: routing (#/view), backend heartbeat, activity log, quit, png
// export and the ?demo walkthrough.

import { getJson, postJson } from './api.js';
import { initJobCancel, setStatus, showError } from './ui.js';
import { initOverview, refreshOverview } from './views/overview.js';
import { initGeometry, inspectHottestNode, loadExamples } from './views/geometry.js';
import { initThermal, plots as thermalPlots, solveLive } from './views/thermal.js';
import { initPerformance, plots as perfPlots, showJob as showPerfJob } from './views/performance.js';
import { discover, initHardware, stopStreams } from './views/hardware.js';
import { initBackprop, plots as bpPlots, showBackprop } from './views/backprop.js';

const $ = (s) => document.querySelector(s);
const plots = { ...thermalPlots, ...perfPlots, ...bpPlots };
const VIEWS = ['overview', 'geometry', 'thermal', 'performance', 'hardware', 'backprop'];
const firstShow = new Set();
let status = null;

function go(name) {
  if (!VIEWS.includes(name)) name = 'overview';
  if (location.hash !== `#/${name}`) history.replaceState(null, '', `${location.pathname}${location.search}#/${name}`);
  for (const b of document.querySelectorAll('.nav button')) {
    if (b.dataset.view === name) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
  }
  for (const v of document.querySelectorAll('.view')) v.hidden = v.id !== `view-${name}`;
  window.scrollTo({ top: 0 });
  if (!firstShow.has(name)) {
    firstShow.add(name);
    if (name === 'thermal') solveLive();
    if (name === 'backprop') showBackprop();
    if (name === 'hardware') discover();
    if (name === 'overview') refreshOverview(status);
  }
}

async function heartbeat() {
  const pill = $('#conn');
  try {
    await getJson('/api/status');
    pill.className = 'pill pill-ok';
    pill.textContent = 'backend running';
  } catch {
    pill.className = 'pill pill-err';
    pill.textContent = 'backend stopped';
  }
}

async function refreshLog() {
  if ($('#log-drawer').hidden) return;
  try {
    const { lines } = await getJson('/api/log');
    const el = $('#log-lines');
    const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 4;
    el.textContent = lines.join('\n');
    if (atBottom) el.scrollTop = el.scrollHeight;
  } catch { /* heartbeat reports connection problems */ }
}

async function start() {
  initJobCancel();
  try {
    status = await getJson('/api/status');
    $('#conn').className = 'pill pill-ok';
    $('#conn').textContent = 'backend running';
    $('#machine').textContent = `${status.system.cpu} · ${status.system.logical_cpus} threads · ready in ${status.ready_ms.toFixed(0)} ms`;
    setStatus(`ready · backend up ${status.ready_ms.toFixed(0)} ms after launch`);
  } catch (e) {
    showError(e, 'startup');
  }

  initOverview(go);
  initGeometry();
  initThermal();
  initPerformance(status);
  initHardware();
  initBackprop();

  for (const b of document.querySelectorAll('.nav button')) b.addEventListener('click', () => go(b.dataset.view));
  for (const b of document.querySelectorAll('[data-goto]')) {
    b.addEventListener('click', () => { go(b.dataset.goto); if (b.hasAttribute('data-demo')) loadExamples(); });
  }
  for (const btn of document.querySelectorAll('[data-png]')) {
    btn.addEventListener('click', () => plots[btn.dataset.png]?.downloadPng(`${btn.dataset.png}.png`));
  }
  $('#log-toggle').addEventListener('click', () => { $('#log-drawer').hidden = !$('#log-drawer').hidden; refreshLog(); });
  $('#log-close').addEventListener('click', () => { $('#log-drawer').hidden = true; });
  $('#quit').addEventListener('click', async () => {
    stopStreams();
    try { await postJson('/api/shutdown'); } catch { /* already gone */ }
    document.body.innerHTML = '<div style="display:grid;place-items:center;height:100vh;font-family:system-ui"><div style="text-align:center"><h1>workbench stopped</h1><p>the backend has shut down. you can close this tab.</p></div></div>';
  });
  window.addEventListener('hashchange', () => go(location.hash.replace('#/', '')));

  const params = new URLSearchParams(location.search);
  const initial = params.get('view') || location.hash.replace('#/', '') || 'overview';
  go(initial);
  if (params.has('demo')) runDemo(initial);
  if (params.has('job')) showPerfJob(params.get('job'));

  setInterval(heartbeat, 3000);
  setInterval(refreshLog, 1000);
}

// ?view=<name>&demo replays the clicks a person would make, for the screenshot
// walkthrough and the demo video. it only presses existing controls.
async function runDemo(view) {
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  const click = (sel) => document.querySelector(sel)?.click();
  if (view === 'geometry') {
    await loadExamples();
    await wait(300);
    inspectHottestNode();
  } else if (view === 'performance') {
    click('#perf-run');
  } else if (view === 'hardware') {
    await wait(500);
    click('#dev-jtag0 [data-fault="timeout_once"]');
    await wait(250);
    click('#dev-jtag0 [data-act="connect"]');
    await wait(700);
    click('#dev-jtag0 [data-act="connect"]');
    await wait(250);
    click('#dev-mcu0 [data-act="connect"]');
    await wait(250);
    for (const id of ['mcu0', 'jtag0']) {
      const s = document.querySelector(`#dev-${id} [data-stream]`);
      s.checked = true;
      s.dispatchEvent(new Event('change'));
    }
  } else if (view === 'backprop') {
    await wait(600);
    const s = $('#bp-step');
    s.value = 1;
    s.dispatchEvent(new Event('input'));
  } else if (view === 'errors') {
    go('geometry');
    const input = $('#csv-file');
    const dt = new DataTransfer();
    dt.items.add(new File(['node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,25\n2,0.1,0,0,NaN\n'], 'broken_example.csv'));
    input.files = dt.files;
    input.dispatchEvent(new Event('change'));
  }
}

start();
