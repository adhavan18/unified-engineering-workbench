// captures the screenshot walkthrough in docs/walkthrough from a running Launch.exe.
// documentation tooling only: it drives the real ui in headless microsoft edge over
// the chrome devtools protocol, pressing the same controls a person would, in real
// time (so live charts and cancellation are shown as they happen).
//
//   build\Launch.exe --no-browser --port 8765
//   node scripts\capture_walkthrough.mjs http://127.0.0.1:8765 docs\walkthrough
//
// needs node 22+ (global WebSocket) and microsoft edge.

import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [app = 'http://127.0.0.1:8765', outDir = 'docs/walkthrough'] = process.argv.slice(2);
const EDGE = [
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
].find(existsSync);
const PORT = 9333;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const profile = join(process.env.TEMP || '.', `wb-capture-${Date.now()}`);
const edge = spawn(EDGE, ['--headless=new', '--disable-gpu-sandbox', `--remote-debugging-port=${PORT}`,
  `--user-data-dir=${profile}`, '--window-size=1440,900', '--hide-scrollbars', 'about:blank'], { stdio: 'ignore' });

let ws;
let nextId = 1;
const pending = new Map();
function send(method, params = {}) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}
async function js(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(`page error: ${r.exceptionDetails.exception?.description || expr}`);
  return r.result.value;
}
async function waitFor(expr, timeoutMs = 20000) {
  const end = Date.now() + timeoutMs;
  while (Date.now() < end) {
    if (await js(`!!(${expr})`)) return;
    await sleep(100);
  }
  throw new Error(`timed out waiting for ${expr}`);
}
async function open(path) {
  await send('Page.navigate', { url: app + path });
  await waitFor(`document.readyState === 'complete' && document.querySelector('#conn')?.textContent === 'backend running'`);
  await sleep(600);
}

const shots = [];
async function shot(file, what) {
  await sleep(250);
  const { data } = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(outDir, file), Buffer.from(data, 'base64'));
  const t = new Date().toTimeString().slice(0, 8);
  shots.push({ t, file, what });
  console.log(`${t}  ${file}`);
}

// sets a text field the way typing does, so the page's own listeners run
const type = (sel, value) => js(`(() => { const el = document.querySelector(${JSON.stringify(sel)});
  el.value = ${JSON.stringify(value)}; el.dispatchEvent(new Event('input', { bubbles: true })); })()`);
const click = (sel) => js(`document.querySelector(${JSON.stringify(sel)}).click()`);
const dropFile = (sel, name, text) => js(`(() => { const input = document.querySelector(${JSON.stringify(sel)});
  const dt = new DataTransfer(); dt.items.add(new File([${JSON.stringify(text)}], ${JSON.stringify(name)}));
  input.files = dt.files; input.dispatchEvent(new Event('change')); })()`);

async function main() {
  mkdirSync(outDir, { recursive: true });
  let target;
  for (let i = 0; i < 50 && !target; i++) {
    await sleep(200);
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = list.find((t) => t.type === 'page');
    } catch { /* edge still starting */ }
  }
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r));
  ws.addEventListener('message', (m) => {
    const msg = JSON.parse(m.data);
    if (msg.id && pending.has(msg.id)) {
      const p = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) p.reject(new Error(msg.error.message)); else p.resolve(msg.result);
    }
  });
  await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });

  await open('/#/overview');
  await waitFor(`!document.querySelector('#proof-C')?.textContent.includes('checking')`);
  await shot('01_overview.png', 'one card per task with a live result from the backend, and the session facts (cpu, cores, compiler, launch-to-ready)');
  await js(`document.querySelector('#ov-checklist').lastElementChild.scrollIntoView({ block: 'center' })`);
  await shot('01b_checklist.png', 'the requirement checklist: every bullet of the brief mapped to the place in the app; docs evidence is ticked only if the file exists');

  await open('/?view=geometry&demo');
  await waitFor(`document.querySelector('.inspector')?.textContent.includes('326')`);
  await shot('02_geometry.png', 'heat-sink stl (mm, converted to m in c++) over its synthetic temperature field; hottest node pinned in the inspector');

  await open('/#/thermal');
  await waitFor(`document.querySelector('#fea-table tbody tr')`);
  await shot('03_thermal.png', 'brief values: peak 188.2 Â°C at x = 0.42 m, end heat flows sum to qAL = 1 W, midpoint errors = qhÂ²/8k, order 2.000');

  await type('#fea-form [name="k"]', '-5');
  await waitFor(`document.querySelector('#fea-form .field-error')`);
  await js('window.scrollTo(0, 0)');
  await shot('04_validation.png', 'k = âˆ’5 typed: the field is flagged inline ("enter a positive number") and the last valid solution stays on screen');
  await click('#fea-brief');
  await sleep(400);

  await type('#meshes', '10,20,40,80,2000000,2500000,3000000,3500000,4000000,4500000');
  await sleep(300);
  await click('#fea-job');
  await waitFor(`!document.querySelector('#job').hidden && parseFloat(document.querySelector('#job-fill').style.width) >= 30`);
  await shot('05_job_progress.png', 'a 19.5-million-node study runs as a background job: progress bar and cancel in the status bar, ui still responsive');
  await click('#job-cancel');
  await waitFor(`document.querySelector('#job').hidden`);
  await shot('06_job_cancelled.png', 'cancelled: the job publishes nothing, so the previous results, plots and exports are unchanged');
  await click('#fea-brief');

  await open('/#/performance');
  await click('#perf-run');
  await waitFor(`document.querySelector('#job-label')?.textContent.match(/run (1[0-9]|[2-9][0-9])/)`, 60000).catch(() => {});
  await shot('07_benchmark_live.png', 'benchmark running: every timed run streams into the live chart as it finishes');
  await waitFor(`document.querySelector('#job').hidden && document.querySelector('#perf-table tbody tr')`, 180000);
  await js(`document.querySelector('#perf-kpis').scrollIntoView({ block: 'start' })`);
  await shot('08_benchmark_done.png', 'finished: median bars with minâ€“max whiskers, speedup vs ideal, efficiency, serial vs parallel max |Î”T|');

  await open('/?view=hardware&demo');
  await waitFor(`document.querySelector('#dev-jtag0')?.textContent.includes('idcode')`, 15000).catch(() => {});
  await sleep(1500);
  await shot('09_hardware.png', 'jtag0 got an injected timeout, faulted, recovered on retry and streams again; mcu0 streams telemetry; all labelled simulated');

  await open('/?view=backprop&demo');
  await sleep(1200);
  await shot('10_backprop.png', 'computation graph at step 1 with forward values and gradients, gradient check 4/4, loss after the handwritten step');

  await open('/#/geometry');
  await dropFile('#csv-file', 'broken_example.csv', 'node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,25\n2,0.1,0,0,NaN\n');
  await waitFor(`document.querySelector('.toast')`);
  await shot('11_error_nan.png', 'a csv with a nan temperature is rejected with the file name, line 3 and a fix-it hint; nothing is loaded');
  await js(`document.querySelectorAll('.toast').forEach((t) => t.remove())`);
  await dropFile('#stl-file', 'empty.stl', '');
  await waitFor(`document.querySelector('.toast')`);
  await shot('12_error_empty.png', 'an empty stl is explained ("the file is empty") instead of failing silently');

  await click('#log-toggle');
  await sleep(1300);
  await shot('13_activity_log.png', 'the activity log drawer: the backend log of everything above, streamed from Launch.exe');

  // table rows for docs/walkthrough/README.md
  console.log('\n' + shots.map((s) => `| ${s.t} | [${s.file}](${s.file}) | ${s.what} |`).join('\n'));
}

main()
  .catch((e) => { console.error(e.message); process.exitCode = 1; })
  .finally(() => { try { ws?.close(); } catch { /* closing */ } edge.kill(); });
