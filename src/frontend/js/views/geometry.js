// geometry & results view: stl + nodal csv in one webgl scene.

import { getText, postBinary } from '../api.js';
import { fmt, setFacts, setStatus, showError, toast } from '../ui.js';
import { Viewer, colormap } from '../viewer3d.js';

const MAX_UPLOAD = 256 * 1024 * 1024;  // matches the backend limit
const $ = (s) => document.querySelector(s);

let viewer = null;
let stlFile = null;     // last stl File, re-parsed when the unit changes
let stlMeta = null;
let field = null;       // { meta, xyz, temp, ids, synthetic }
let pinned = -1;

export function initGeometry() {
  try {
    viewer = new Viewer($('#geo-canvas'), { gizmo: $('#geo-gizmo'), onHover: showTooltip, onPick: pin });
  } catch (e) {
    toast('3d view unavailable', { hint: `${e.message}. parsing and validation still work.` });
  }

  $('#stl-file').addEventListener('change', (e) => { loadFile(e.target.files[0]); e.target.value = ''; });
  $('#csv-file').addEventListener('change', (e) => { loadFile(e.target.files[0]); e.target.value = ''; });
  for (const id of ['#geo-examples', '#geo-examples-2']) $(id).addEventListener('click', loadExamples);

  // drag and drop anywhere on the viewer
  const wrap = $('#geo-wrap');
  wrap.addEventListener('dragover', (e) => { e.preventDefault(); wrap.classList.add('dragging'); });
  wrap.addEventListener('dragleave', () => wrap.classList.remove('dragging'));
  wrap.addEventListener('drop', (e) => {
    e.preventDefault();
    wrap.classList.remove('dragging');
    for (const f of e.dataTransfer.files) loadFile(f);
  });

  for (const b of document.querySelectorAll('#geo-views button')) {
    b.addEventListener('click', () => {
      for (const o of document.querySelectorAll('#geo-views button')) o.setAttribute('aria-pressed', String(o === b));
      viewer?.setView(b.dataset.v);
    });
  }
  $('#geo-fit').addEventListener('click', () => viewer?.fit(false));
  $('#geo-png').addEventListener('click', () => {
    if (!viewer) return;
    const a = document.createElement('a');
    a.href = viewer.snapshot();
    a.download = 'workbench_3d_view.png';
    a.click();
  });

  $('#show-mesh').addEventListener('change', (e) => viewer?.setVisible('mesh', e.target.checked));
  $('#show-points').addEventListener('change', (e) => viewer?.setVisible('points', e.target.checked));
  $('#wireframe').addEventListener('change', (e) => viewer?.setVisible('wire', e.target.checked));
  $('#mesh-opacity').addEventListener('input', (e) => {
    $('#opacity-val').textContent = `${e.target.value} %`;
    viewer?.setMeshOpacity(e.target.value / 100);
  });
  $('#point-size').addEventListener('input', (e) => {
    $('#size-val').textContent = `${e.target.value} px`;
    viewer?.setPointSize(+e.target.value);
  });
  $('#stl-units').addEventListener('change', () => { if (stlFile) loadStl(stlFile); });
  for (const b of document.querySelectorAll('#cmap button')) {
    b.addEventListener('click', () => {
      for (const o of document.querySelectorAll('#cmap button')) o.setAttribute('aria-pressed', String(o === b));
      viewer?.setColormap(b.dataset.c);
      renderLegend();
    });
  }
  $('#threshold').addEventListener('input', applyThreshold);
  for (const id of ['#range-lo', '#range-hi']) $(id).addEventListener('input', applyRange);
  $('#range-auto').addEventListener('click', () => {
    if (!field) return;
    $('#range-lo').value = round(field.meta.t_min_C);
    $('#range-hi').value = round(field.meta.t_max_C);
    applyRange();
  });
  $('#hottest').addEventListener('click', inspectHottestNode);
}

const round = (v) => Number(v.toPrecision(5));
const cmapName = () => document.querySelector('#cmap [aria-pressed="true"]')?.dataset.c || 'inferno';

export async function loadExamples() {
  try {
    const [stl, csv] = await Promise.all([getText('/api/examples/stl'), getText('/api/examples/csv?which=heatsink')]);
    $('#stl-units').value = 'mm';
    await loadStl(new File([stl.text], stl.name));
    await loadCsv(new File([csv.text], csv.name));
    $('#mesh-opacity').value = 25;
    $('#opacity-val').textContent = '25 %';
    viewer?.setMeshOpacity(0.25);
    setStatus('examples loaded: heat-sink stl (mm) with its synthetic temperature field (m), overlaid');
  } catch (e) {
    showError(e, 'examples');
  }
}

function loadFile(file) {
  if (!file) return;
  if (file.size > MAX_UPLOAD) {
    toast(`${file.name} is ${(file.size / 1048576).toFixed(0)} MB`, { hint: 'the limit is 256 MB. decimate or split the file.' });
    return;
  }
  const ext = file.name.split('.').pop().toLowerCase();
  if (ext === 'stl') loadStl(file);
  else if (ext === 'csv' || ext === 'txt') loadCsv(file);
  else toast(`${file.name}: unsupported file type`, { hint: 'use an ascii .stl or a .csv with node_id,x_m,y_m,z_m,temperature_C' });
}

async function loadStl(file) {
  stlFile = file;
  const unit = $('#stl-units').value;
  setStatus(`parsing ${file.name}…`);
  const t0 = performance.now();
  try {
    const { meta, arrays } = await postBinary(`/api/geometry/stl?name=${encodeURIComponent(file.name)}&unit=${unit}`, file);
    stlMeta = { ...meta, ms: performance.now() - t0 };
    viewer?.setMesh(arrays.positions, arrays.normals, meta.bbox_min, meta.bbox_max);
    $('#geo-drop').hidden = true;
    const size = [0, 1, 2].map((a) => (meta.bbox_max[a] - meta.bbox_min[a]) / meta.metres_per_unit);
    setFacts($('#stl-info'), [
      ['file', meta.file],
      ['solid', meta.solid_name || '(unnamed)'],
      ['triangles', meta.triangles.toLocaleString()],
      ['zero-area triangles', String(meta.degenerate_triangles)],
      ['size', `${size.map((v) => fmt(v, 4)).join(' × ')} ${unit}  (${size.map((v) => fmt(v * meta.metres_per_unit, 3)).join(' × ')} m)`],
      ['file size', `${(meta.bytes / 1024).toFixed(1)} kB`],
      ['parse + transfer', `${stlMeta.ms.toFixed(0)} ms`],
    ]);
    $('#stl-status').innerHTML = '<span class="pill pill-ok">loaded</span>';
    setStatus(`loaded ${file.name}: ${meta.triangles.toLocaleString()} triangles`);
    if (meta.degenerate_triangles > 0) {
      toast(`${meta.degenerate_triangles} zero-area triangles`, { kind: 'info', hint: 'they are kept but have no visible area' });
    }
  } catch (e) {
    $('#stl-status').innerHTML = '<span class="pill pill-err">rejected</span>';
    showError(e);  // backend messages already start with the file name
  }
}

async function loadCsv(file) {
  setStatus(`parsing ${file.name}…`);
  const t0 = performance.now();
  try {
    const { meta, arrays } = await postBinary(`/api/results/nodal-csv?name=${encodeURIComponent(file.name)}`, file);
    const ms = performance.now() - t0;
    field = { meta, xyz: arrays.xyz_m, temp: arrays.temperature_C, ids: arrays.node_id, synthetic: /synthetic/i.test(file.name) };
    viewer?.setPoints(arrays.xyz_m, arrays.temperature_C, meta.bbox_min_m, meta.bbox_max_m);
    viewer?.setColormap(cmapName());
    // denser clouds read better with smaller dots
    const dot = meta.nodes > 50000 ? 2 : meta.nodes > 3000 ? 4 : 6;
    $('#point-size').value = dot;
    $('#size-val').textContent = `${dot} px`;
    viewer?.setPointSize(dot);
    $('#geo-drop').hidden = true;
    pin(-1);

    $('#range-lo').value = round(meta.t_min_C);
    $('#range-hi').value = round(meta.t_max_C);
    const thr = $('#threshold');
    thr.disabled = false;
    thr.value = 0;
    applyThreshold();
    renderLegend();

    const badge = $('#csv-badge');
    badge.replaceChildren();
    if (field.synthetic) {
      const n = document.createElement('div');
      n.className = 'notice notice-sim';
      n.style.marginBottom = '10px';
      n.textContent = 'synthetic data, generated for this assignment. not an actual ansys export (see data/PROVENANCE.md).';
      badge.appendChild(n);
    }
    const size = [0, 1, 2].map((a) => meta.bbox_max_m[a] - meta.bbox_min_m[a]);
    setFacts($('#csv-info'), [
      ['file', meta.file],
      ['nodes', meta.nodes.toLocaleString()],
      ['temperature', `${fmt(meta.t_min_C, 5)} … ${fmt(meta.t_max_C, 5)} °C`],
      ['extent', `${size.map((v) => fmt(v, 3)).join(' × ')} m`],
      ['blank lines skipped', String(meta.blank_lines)],
      ['parse + transfer', `${ms.toFixed(0)} ms`],
    ]);
    $('#csv-status').innerHTML = '<span class="pill pill-ok">loaded</span>';
    setStatus(`loaded ${file.name}: ${meta.nodes.toLocaleString()} nodes`);
  } catch (e) {
    $('#csv-status').innerHTML = '<span class="pill pill-err">rejected</span>';
    showError(e);
  }
}

function applyThreshold() {
  if (!field) return;
  const { t_min_C: lo, t_max_C: hi } = field.meta;
  const t = lo + (hi - lo) * ($('#threshold').value / 1000);
  $('#thr-val').textContent = `${fmt(t, 4)} °C`;
  viewer?.setThreshold(t);
  if (pinned >= 0 && field.temp[pinned] < t) pin(-1);
}

function applyRange() {
  const lo = +$('#range-lo').value, hi = +$('#range-hi').value;
  if (!Number.isFinite(lo) || !Number.isFinite(hi) || hi <= lo) return;
  viewer?.setColorRange(lo, hi);
  renderLegend();
}

function renderLegend() {
  if (!field) return;
  $('#geo-legend').hidden = false;
  $('#geo-legend-bar').style.background = colormap(cmapName()).css;
  const lo = +$('#range-lo').value, hi = +$('#range-hi').value;
  const ticks = $('#geo-ticks');
  ticks.replaceChildren();
  for (let k = 0; k <= 5; k++) {
    const s = document.createElement('span');
    s.style.bottom = `${(k / 5) * 100}%`;
    s.textContent = fmt(lo + ((hi - lo) * k) / 5, 4);
    ticks.appendChild(s);
  }
}

function nodeFacts(i) {
  return [
    ['node id', String(field.ids[i])],
    ['x, y, z', `${[0, 1, 2].map((a) => fmt(field.xyz[3 * i + a], 4)).join(', ')} m`],
    ['temperature', `${fmt(field.temp[i], 6)} °C`],
  ];
}

function showTooltip(i, x, y) {
  const tip = $('#geo-tip');
  if (i < 0 || !field) { tip.hidden = true; return; }
  tip.hidden = false;
  tip.style.left = `${x}px`;
  tip.style.top = `${y}px`;
  tip.innerHTML = '';
  const b = document.createElement('b');
  b.textContent = `${fmt(field.temp[i], 5)} °C`;
  const d = document.createElement('div');
  d.className = 'muted';
  d.textContent = `node ${field.ids[i]} · (${[0, 1, 2].map((a) => fmt(field.xyz[3 * i + a] * 1000, 3)).join(', ')}) mm`;
  tip.append(b, d);
}

function pin(i) {
  pinned = i;
  if (i < 0 || !field) {
    setFacts($('#pick-info'), [['node', 'hover or click a node']]);
    $('#pick-info dd').className = 'muted';
    viewer?.setPicked(-1);
    return;
  }
  setFacts($('#pick-info'), nodeFacts(i));
}

// pins the hottest node (used by the "hottest" button and the ?demo walkthrough)
export function inspectHottestNode() {
  if (!field) return;
  let hot = 0;
  for (let i = 1; i < field.temp.length; i++) if (field.temp[i] > field.temp[hot]) hot = i;
  viewer?.setPicked(hot);
  pin(hot);
}
