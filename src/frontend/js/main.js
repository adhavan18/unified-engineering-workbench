// app shell: tabs, backend heartbeat, activity log, quit, png export.

import { getJson, postJson } from './api.js';
import { initJobCancel, setStatus, showError, toast } from './ui.js';
import { initGeometry, inspectHottestNode } from './views/geometry.js';
import { initThermal, plots as thermalPlots } from './views/thermal.js';
import { initPerformance, plots as perfPlots, showJob as showPerfJob } from './views/performance.js';
import { initHardware } from './views/hardware.js';
import { initBackprop, showBackprop, plots as bpPlots } from './views/backprop.js';

const $ = (s) => document.querySelector(s);
const plots = { ...thermalPlots, ...perfPlots, ...bpPlots };

function selectView(name) {
  for (const tab of document.querySelectorAll('.tabs button')) {
    tab.setAttribute('aria-selected', String(tab.dataset.view === name));
  }
  for (const v of document.querySelectorAll('.view')) v.hidden = v.id !== `view-${name}`;
  if (name === 'backprop') showBackprop();
  try { localStorage.setItem('wb.view', name); } catch { /* storage may be blocked */ }
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
  for (const tab of document.querySelectorAll('.tabs button')) {
    tab.addEventListener('click', () => selectView(tab.dataset.view));
  }
  initJobCancel();

  let status = null;
  try {
    status = await getJson('/api/status');
    $('#conn').className = 'pill pill-ok';
    $('#conn').textContent = 'backend running';
    setStatus(`ready · backend up ${status.ready_ms.toFixed(0)} ms after launch · ${status.system.cpu}`);
  } catch (e) {
    showError(e, 'startup');
  }

  initGeometry();
  initThermal();
  initPerformance(status);
  initHardware();
  initBackprop();

  for (const btn of document.querySelectorAll('[data-png]')) {
    btn.addEventListener('click', () => plots[btn.dataset.png]?.downloadPng(`${btn.dataset.png}.png`));
  }
  $('#log-toggle').addEventListener('click', () => { $('#log-drawer').hidden = !$('#log-drawer').hidden; refreshLog(); });
  $('#log-close').addEventListener('click', () => { $('#log-drawer').hidden = true; });
  $('#quit').addEventListener('click', async () => {
    try { await postJson('/api/shutdown'); } catch { /* already gone */ }
    document.body.innerHTML = '<main><div class="card"><h1>workbench stopped</h1><p>the backend has shut down. you can close this tab.</p></div></main>';
  });

  let saved = 'geometry';
  try { saved = localStorage.getItem('wb.view') || 'geometry'; } catch { /* ignore */ }
  const params = new URLSearchParams(location.search);
  selectView(params.get('view') || saved);
  if (params.has('demo')) runDemo(params.get('view') || saved);
  if (params.has('job')) showPerfJob(params.get('job'));

  setInterval(heartbeat, 3000);
  setInterval(refreshLog, 1000);
  if (status && !status.system) toast('backend status is incomplete', { kind: 'info' });
}

// ?view=<name>&demo replays the example steps a person would click, for the
// screenshot walkthrough and the demo video. it only presses existing buttons.
async function runDemo(view) {
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  const click = (sel) => document.querySelector(sel)?.click();
  if (view === 'geometry') {
    click('#stl-example');
    await wait(400);
    click('#csv-example');
    await wait(700);
    inspectHottestNode();
  } else if (view === 'thermal') {
    click('#fea-run');
  } else if (view === 'performance') {
    click('#perf-run');
  } else if (view === 'hardware') {
    click('#hw-discover');
    await wait(400);
    const jtag = $('#dev-jtag0 [data-fault]');
    jtag.value = 'timeout_once';
    jtag.dispatchEvent(new Event('change'));
    await wait(300);
    click('#dev-jtag0 [data-act="connect"]');
    await wait(700);
    click('#dev-jtag0 [data-act="connect"]');
    await wait(300);
    click('#dev-jtag0 [data-act="telemetry"]');
    click('#dev-mcu0 [data-act="connect"]');
    await wait(300);
    click('#dev-mcu0 [data-act="telemetry"]');
  } else if (view === 'errors') {
    selectView('geometry');
    const input = $('#csv-file');
    const dt = new DataTransfer();
    dt.items.add(new File(['node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,25\n2,0.1,0,0,NaN\n'], 'broken_example.csv'));
    input.files = dt.files;
    input.dispatchEvent(new Event('change'));
  }
}

start();
