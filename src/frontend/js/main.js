// app shell: tabs, backend heartbeat, activity log, quit, png export.

import { getJson, postJson } from './api.js';
import { initJobCancel, setStatus, showError, toast } from './ui.js';
import { initGeometry } from './views/geometry.js';
import { initThermal, plots as thermalPlots } from './views/thermal.js';
import { initPerformance, plots as perfPlots } from './views/performance.js';
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
  selectView(saved);

  setInterval(heartbeat, 3000);
  setInterval(refreshLog, 1000);
  if (status && !status.system) toast('backend status is incomplete', { kind: 'info' });
}

start();
