// geometry view: ascii stl viewer and nodal-temperature point cloud.

import { getText, postBinary } from '../api.js';
import { setFacts, setStatus, showError, toast, fmt } from '../ui.js';
import { Viewer, viridis } from '../viewer3d.js';

const MAX_UPLOAD = 256 * 1024 * 1024;  // matches the backend limit

const $ = (s) => document.querySelector(s);

let stlViewer, csvViewer;
let stlMeta = null;
let field = null;  // { meta, xyz, temp, ids }

export function initGeometry() {
  try {
    stlViewer = new Viewer($('#stl-canvas'));
    csvViewer = new Viewer($('#csv-canvas'), { onPick: showNode });
  } catch (e) {
    toast('3d view unavailable', { hint: `${e.message}. file parsing still works.` });
  }

  $('#stl-file').addEventListener('change', (e) => { loadStl(e.target.files[0]); e.target.value = ''; });
  $('#csv-file').addEventListener('change', (e) => { loadCsv(e.target.files[0]); e.target.value = ''; });
  $('#stl-example').addEventListener('click', async () => {
    try {
      const { text, name } = await getText('/api/examples/stl');
      loadStl(new File([text], name || 'heatsink_ascii.stl'));
    } catch (e) { showError(e, 'example stl'); }
  });
  $('#csv-example').addEventListener('click', async () => {
    try {
      const { text, name } = await getText('/api/examples/csv');
      loadCsv(new File([text], name || 'rod_nodal_temperatures_synthetic.csv'), true);
    } catch (e) { showError(e, 'example csv'); }
  });
  $('#stl-fit').addEventListener('click', () => stlViewer?.fit());
  $('#stl-reset').addEventListener('click', () => stlViewer?.reset());
  $('#csv-fit').addEventListener('click', () => csvViewer?.fit());
  $('#csv-size').addEventListener('input', (e) => csvViewer?.setPointSize(+e.target.value));
  $('#stl-units').addEventListener('change', renderStlFacts);
}

function checkSize(file) {
  if (file.size > MAX_UPLOAD) {
    toast(`${file.name} is ${(file.size / 1048576).toFixed(0)} MB`, { hint: 'the limit is 256 MB. decimate or split the file.' });
    return false;
  }
  return true;
}

async function loadStl(file) {
  if (!file || !checkSize(file)) return;
  setStatus(`parsing ${file.name}…`);
  const t0 = performance.now();
  try {
    const { meta, arrays } = await postBinary(`/api/geometry/stl?name=${encodeURIComponent(file.name)}`, file);
    stlMeta = { ...meta, ms: performance.now() - t0 };
    stlViewer?.setMesh(arrays.positions, arrays.normals, meta.bbox_min, meta.bbox_max);
    $('#stl-empty').hidden = true;
    renderStlFacts();
    setStatus(`loaded ${file.name}: ${meta.triangles.toLocaleString()} triangles`);
    if (meta.degenerate_triangles > 0) {
      toast(`${meta.degenerate_triangles} zero-area triangles`, { kind: 'info', hint: 'they are drawn but have no visible area; the model may need cleaning' });
    }
  } catch (e) {
    showError(e);  // backend messages already start with the file name
  }
}

function renderStlFacts() {
  if (!stlMeta) return;
  // stl coordinates are plain numbers in whatever unit the exporter used,
  // so the selected unit labels the values rather than rescaling them
  const unit = $('#stl-units').value;
  const size = [0, 1, 2].map((a) => stlMeta.bbox_max[a] - stlMeta.bbox_min[a]);
  setFacts($('#stl-info'), [
    ['file', stlMeta.file],
    ['solid name', stlMeta.solid_name || '(none)'],
    ['triangles', stlMeta.triangles.toLocaleString()],
    ['zero-area triangles', String(stlMeta.degenerate_triangles)],
    ['size x × y × z', `${size.map((v) => fmt(v, 5)).join(' × ')} ${unit}`],
    ['file size', `${(stlMeta.bytes / 1024).toFixed(1)} kB`],
    ['parse + transfer', `${stlMeta.ms.toFixed(0)} ms`],
  ]);
}

async function loadCsv(file, synthetic = false) {
  if (!file || !checkSize(file)) return;
  setStatus(`parsing ${file.name}…`);
  const t0 = performance.now();
  try {
    const { meta, arrays } = await postBinary(`/api/results/nodal-csv?name=${encodeURIComponent(file.name)}`, file);
    const ms = performance.now() - t0;
    const temp = arrays.temperature_C;
    const span = meta.t_max_C - meta.t_min_C || 1;
    const colors = new Float32Array(temp.length * 3);
    for (let i = 0; i < temp.length; i++) colors.set(viridis((temp[i] - meta.t_min_C) / span), 3 * i);
    field = { meta, xyz: arrays.xyz_m, temp, ids: arrays.node_id };
    csvViewer?.setPoints(arrays.xyz_m, colors, meta.bbox_min_m, meta.bbox_max_m);
    $('#csv-empty').hidden = true;
    $('#csv-pick').hidden = true;
    renderLegend(meta.t_min_C, meta.t_max_C);

    const badge = $('#csv-badge');
    badge.replaceChildren();
    const isSynthetic = synthetic || /synthetic/i.test(file.name);
    if (isSynthetic) {
      const n = document.createElement('div');
      n.className = 'notice notice-sim';
      n.textContent = 'synthetic data: generated from the analytical rod solution, not an actual ansys export.';
      badge.appendChild(n);
    }
    const size = [0, 1, 2].map((a) => meta.bbox_max_m[a] - meta.bbox_min_m[a]);
    setFacts($('#csv-info'), [
      ['file', meta.file],
      ['nodes', meta.nodes.toLocaleString()],
      ['temperature range', `${fmt(meta.t_min_C, 6)} … ${fmt(meta.t_max_C, 6)} °C`],
      ['extent x × y × z', `${size.map((v) => fmt(v, 4)).join(' × ')} m`],
      ['blank lines skipped', String(meta.blank_lines)],
      ['parse + transfer', `${ms.toFixed(0)} ms`],
    ]);
    setStatus(`loaded ${file.name}: ${meta.nodes.toLocaleString()} nodes`);
  } catch (e) {
    showError(e);
  }
}

function renderLegend(lo, hi) {
  $('#csv-legend').hidden = false;
  const ticks = $('#csv-ticks');
  ticks.replaceChildren();
  for (let k = 0; k <= 4; k++) {
    const t = k / 4;
    const s = document.createElement('span');
    s.style.bottom = `${t * 100}%`;
    s.textContent = fmt(lo + (hi - lo) * t, 4);
    ticks.appendChild(s);
  }
}

// used by the ?demo walkthrough: inspect the hottest node without a mouse
export function inspectHottestNode() {
  if (!field) return;
  let hot = 0;
  for (let i = 1; i < field.temp.length; i++) if (field.temp[i] > field.temp[hot]) hot = i;
  csvViewer?.setPicked(hot);
  showNode(hot);
}

function showNode(i) {
  const box = $('#csv-pick');
  if (i < 0 || !field) { box.hidden = true; return; }
  box.hidden = false;
  box.replaceChildren();
  const dl = document.createElement('dl');
  dl.className = 'facts';
  box.appendChild(dl);
  setFacts(dl, [
    ['node id', String(field.ids[i])],
    ['x, y, z', `${[0, 1, 2].map((a) => fmt(field.xyz[3 * i + a], 6)).join(', ')} m`],
    ['temperature', `${fmt(field.temp[i], 8)} °C`],
  ]);
}
