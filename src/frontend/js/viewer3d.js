// webgl viewer for stl triangles and temperature point clouds.
// orbit (drag), pan (right/shift drag), zoom (wheel), animated view presets,
// grid floor, axis gizmo, wireframe, mesh opacity, colour-mapped nodes with a
// gpu-side colour range and threshold, hover + click picking. z is up.

const FOV = (38 * Math.PI) / 180;

const VS_MESH = `
attribute vec3 aPos; attribute vec3 aNormal;
uniform mat4 uMVP; varying vec3 vN;
void main() { vN = aNormal; gl_Position = uMVP * vec4(aPos, 1.0); }`;
const FS_MESH = `
precision mediump float; varying vec3 vN;
uniform vec3 uLight; uniform vec3 uBase; uniform float uAlpha;
void main() {
  vec3 n = normalize(vN);
  float d = abs(dot(n, uLight));                       // two-sided headlight
  float rim = pow(1.0 - d, 3.0) * 0.18;                 // soft edge highlight
  gl_FragColor = vec4(uBase * (0.32 + 0.68 * d) + rim, uAlpha);
}`;
const VS_LINE = `
attribute vec3 aPos; attribute vec3 aColor; uniform mat4 uMVP; varying vec3 vC;
void main() { vC = aColor; gl_Position = uMVP * vec4(aPos, 1.0); }`;
const FS_LINE = `
precision mediump float; varying vec3 vC; uniform float uAlpha;
void main() { gl_FragColor = vec4(vC, uAlpha); }`;
const VS_PTS = `
attribute vec3 aPos; attribute float aT;
uniform mat4 uMVP; uniform float uSize; uniform float uMinVisible;
varying float vT;
void main() {
  vT = aT;
  if (aT < uMinVisible) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); gl_PointSize = 0.0; return; }
  gl_Position = uMVP * vec4(aPos, 1.0);
  gl_PointSize = uSize;
}`;
const FS_PTS = `
precision mediump float; varying float vT;
uniform sampler2D uLut; uniform vec2 uRange;
void main() {
  vec2 p = gl_PointCoord - vec2(0.5);
  float r = length(p);
  if (r > 0.5) discard;
  float t = clamp((vT - uRange.x) / max(uRange.y - uRange.x, 1e-9), 0.0, 1.0);
  vec3 c = texture2D(uLut, vec2(t, 0.5)).rgb;
  c *= 1.0 - 0.18 * smoothstep(0.3, 0.5, r);             // light sphere shading
  gl_FragColor = vec4(c, 1.0);
}`;
const VS_PICK = `
attribute vec3 aPos; uniform mat4 uMVP; uniform float uSize;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); gl_PointSize = uSize; }`;
const FS_PICK = `
precision mediump float; uniform vec3 uColor;
void main() { if (length(gl_PointCoord - vec2(0.5)) > 0.5) discard; gl_FragColor = vec4(uColor, 1.0); }`;

// ---------- colour maps (perceptual stops, interpolated to a 256 lut) ----------
const MAPS = {
  inferno: [[0, 0, 4], [31, 12, 72], [85, 15, 109], [136, 34, 106], [186, 54, 85], [227, 89, 51], [249, 140, 10], [249, 201, 50], [252, 255, 164]],
  viridis: [[68, 1, 84], [71, 44, 122], [59, 81, 139], [44, 113, 142], [33, 144, 141], [39, 173, 129], [92, 200, 99], [170, 220, 50], [253, 231, 37]],
  coolwarm: [[59, 76, 192], [124, 159, 249], [221, 221, 221], [246, 160, 128], [180, 4, 38]],
};

export function colormap(name) {
  const stops = MAPS[name] || MAPS.inferno;
  const lut = new Uint8Array(256 * 4);
  for (let i = 0; i < 256; i++) {
    const x = (i / 255) * (stops.length - 1);
    const k = Math.min(stops.length - 2, Math.floor(x)), f = x - k;
    for (let c = 0; c < 3; c++) lut[4 * i + c] = Math.round(stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f);
    lut[4 * i + 3] = 255;
  }
  const css = `linear-gradient(to top, ${stops.map((s, i) => `rgb(${s.join(',')}) ${(i / (stops.length - 1)) * 100}%`).join(', ')})`;
  return { lut, css };
}

// ---------- tiny column-major mat4 helpers ----------
function perspective(fovy, aspect, near, far) {
  const f = 1 / Math.tan(fovy / 2), m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = (far + near) / (near - far); m[11] = -1; m[14] = (2 * far * near) / (near - far);
  return m;
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
function lookAt(eye, center, up) {
  const z = norm(sub(eye, center)), x = norm(cross(up, z)), y = cross(z, x);
  return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]);
}
function multiply(a, b) {
  const o = new Float32Array(16);
  for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) {
    let s = 0;
    for (let k = 0; k < 4; k++) s += a[i + 4 * k] * b[k + 4 * j];
    o[i + 4 * j] = s;
  }
  return o;
}
function niceStep(raw) {
  const mag = 10 ** Math.floor(Math.log10(raw));
  return [1, 2, 5, 10].map((m) => m * mag).find((s) => s >= raw) || 10 * mag;
}

function compile(gl, vs, fs) {
  const p = gl.createProgram();
  for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    gl.attachShader(p, s);
  }
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  const loc = {};
  const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < n; i++) { const u = gl.getActiveUniform(p, i); loc[u.name] = gl.getUniformLocation(p, u.name); }
  const na = gl.getProgramParameter(p, gl.ACTIVE_ATTRIBUTES);
  for (let i = 0; i < na; i++) { const a = gl.getActiveAttrib(p, i); loc[a.name] = gl.getAttribLocation(p, a.name); }
  return { p, loc };
}

const VIEWS = {
  iso: { yaw: -Math.PI * 0.32, pitch: Math.PI * 0.2 },
  top: { yaw: -Math.PI / 2, pitch: Math.PI / 2 - 0.001 },
  front: { yaw: -Math.PI / 2, pitch: 0 },
  right: { yaw: 0, pitch: 0 },
};

export class Viewer {
  constructor(canvas, { gizmo, onHover, onPick } = {}) {
    this.canvas = canvas;
    this.gizmo = gizmo;
    this.onHover = onHover;
    this.onPick = onPick;
    const gl = canvas.getContext('webgl', { antialias: true, preserveDrawingBuffer: true, premultipliedAlpha: false, alpha: true });
    if (!gl) throw new Error('webgl is not available in this browser');
    this.gl = gl;
    this.prog = { mesh: compile(gl, VS_MESH, FS_MESH), line: compile(gl, VS_LINE, FS_LINE), pts: compile(gl, VS_PTS, FS_PTS), pick: compile(gl, VS_PICK, FS_PICK) };
    this.buf = {};
    this.mesh = null;
    this.points = null;
    this.show = { mesh: true, points: true, wire: false };
    this.meshAlpha = 1;
    this.pointSize = 5;
    this.minVisible = -1;  // normalised temperature
    this.colorRange = [0, 1];
    this.picked = -1;
    this.hovered = -1;
    this.cam = { yaw: VIEWS.iso.yaw, pitch: VIEWS.iso.pitch, dist: 1, target: [0, 0, 0] };
    this.goal = null;
    this.lut = gl.createTexture();
    this.setColormap('inferno');
    this.dirty = true;
    this.bindInput();
    new ResizeObserver(() => { this.dirty = true; }).observe(canvas);
    this.loop();
  }

  // ---------------- data ----------------
  setMesh(positions, faceNormals, min, max) {
    const tris = positions.length / 9;
    const normals = new Float32Array(positions.length);
    for (let t = 0; t < tris; t++) {
      const n = faceNormals.subarray(3 * t, 3 * t + 3);
      normals.set(n, 9 * t); normals.set(n, 9 * t + 3); normals.set(n, 9 * t + 6);
    }
    this.upload('meshPos', positions);
    this.upload('meshNrm', normals);
    this.mesh = { count: tris * 3, positions, min, max, wireCount: 0 };
    if (this.show.wire) this.buildWire();
    this.updateBounds(true);
  }

  setPoints(xyz, temps, min, max) {
    let lo = Infinity, hi = -Infinity;
    for (const t of temps) { if (t < lo) lo = t; if (t > hi) hi = t; }
    const span = hi - lo || 1;
    const norm01 = new Float32Array(temps.length);
    for (let i = 0; i < temps.length; i++) norm01[i] = (temps[i] - lo) / span;
    this.upload('ptsPos', xyz);
    this.upload('ptsT', norm01);
    this.points = { count: temps.length, xyz, temps, norm01, lo, hi, min, max };
    this.colorRange = [0, 1];
    this.minVisible = -1;
    this.picked = this.hovered = -1;
    this.updateBounds(true);
  }

  clearMesh() { this.mesh = null; this.updateBounds(false); }
  clearPoints() { this.points = null; this.updateBounds(false); }

  setColormap(name) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.lut);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 256, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, colormap(name).lut);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.dirty = true;
  }

  // degrees celsius in, normalised internally
  setColorRange(loC, hiC) {
    if (!this.points) return;
    const { lo, hi } = this.points, span = hi - lo || 1;
    this.colorRange = [(loC - lo) / span, (hiC - lo) / span];
    this.dirty = true;
  }

  setThreshold(tC) {
    if (!this.points) return;
    const { lo, hi } = this.points;
    this.minVisible = tC <= lo ? -1 : (tC - lo) / (hi - lo || 1) - 1e-6;
    this.dirty = true;
  }

  setPointSize(px) { this.pointSize = px; this.dirty = true; }
  setMeshOpacity(a) { this.meshAlpha = a; this.dirty = true; }
  setVisible(what, on) { this.show[what] = on; if (what === 'wire' && on && this.mesh && !this.mesh.wireCount) this.buildWire(); this.dirty = true; }

  setPicked(i) {
    this.picked = i;
    if (i >= 0) this.upload('pickPos', this.points.xyz.slice(3 * i, 3 * i + 3));
    this.dirty = true;
  }

  setView(name) {
    const v = VIEWS[name] || VIEWS.iso;
    this.goal = { yaw: v.yaw, pitch: v.pitch };
    this.fit(false);
  }

  fit(resetAngles = false) {
    if (!this.bounds) return;
    this.cam.target = this.bounds.center.slice();
    this.cam.dist = (this.bounds.radius / Math.sin(FOV / 2)) * 1.05;
    if (resetAngles) this.goal = { ...VIEWS.iso };
    this.dirty = true;
  }

  snapshot() {
    this.draw();
    return this.canvas.toDataURL('image/png');
  }

  // ---------------- internals ----------------
  upload(name, data) {
    const gl = this.gl;
    if (!this.buf[name]) this.buf[name] = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buf[name]);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  }

  buildWire() {
    const p = this.mesh.positions;
    const tris = p.length / 9;
    if (tris > 400000) { this.mesh.wireCount = 0; return; }  // too dense to be useful
    const out = new Float32Array(tris * 18);
    const col = new Float32Array(tris * 18).fill(0.18);
    for (let t = 0; t < tris; t++) {
      const b = 9 * t, o = 18 * t;
      const v = [p.subarray(b, b + 3), p.subarray(b + 3, b + 6), p.subarray(b + 6, b + 9)];
      out.set(v[0], o); out.set(v[1], o + 3); out.set(v[1], o + 6); out.set(v[2], o + 9); out.set(v[2], o + 12); out.set(v[0], o + 15);
    }
    this.upload('wirePos', out);
    this.upload('wireCol', col);
    this.mesh.wireCount = tris * 6;
  }

  updateBounds(refit) {
    const boxes = [];
    if (this.mesh) boxes.push([this.mesh.min, this.mesh.max]);
    if (this.points) boxes.push([this.points.min, this.points.max]);
    if (!boxes.length) { this.bounds = null; this.dirty = true; return; }
    const min = [0, 1, 2].map((a) => Math.min(...boxes.map((b) => b[0][a])));
    const max = [0, 1, 2].map((a) => Math.max(...boxes.map((b) => b[1][a])));
    const size = [0, 1, 2].map((a) => max[a] - min[a]);
    const radius = Math.max(Math.hypot(...size) / 2, 1e-9);
    this.bounds = { min, max, center: [0, 1, 2].map((a) => (min[a] + max[a]) / 2), radius };
    this.buildScaffold();
    if (refit) this.fit(false);
    this.dirty = true;
  }

  // grid floor under the model plus a bounding box
  buildScaffold() {
    const { min, max } = this.bounds;
    const sx = max[0] - min[0], sy = max[1] - min[1];
    const ext = Math.max(sx, sy, this.bounds.radius) * 0.85;
    const step = niceStep(ext / 6);
    const cx = (min[0] + max[0]) / 2, cy = (min[1] + max[1]) / 2, z = min[2];
    const x0 = Math.floor((cx - ext) / step) * step, x1 = Math.ceil((cx + ext) / step) * step;
    const y0 = Math.floor((cy - ext) / step) * step, y1 = Math.ceil((cy + ext) / step) * step;
    const pos = [], col = [];
    const g = [0.55, 0.58, 0.64];
    for (let x = x0; x <= x1 + step / 2; x += step) { pos.push(x, y0, z, x, y1, z); col.push(...g, ...g); }
    for (let y = y0; y <= y1 + step / 2; y += step) { pos.push(x0, y, z, x1, y, z); col.push(...g, ...g); }
    this.gridInfo = { step };
    this.upload('gridPos', new Float32Array(pos));
    this.upload('gridCol', new Float32Array(col));
    this.gridCount = pos.length / 3;
  }

  matrices() {
    const { yaw, pitch, dist, target } = this.cam;
    const eye = [target[0] + dist * Math.cos(pitch) * Math.cos(yaw), target[1] + dist * Math.cos(pitch) * Math.sin(yaw), target[2] + dist * Math.sin(pitch)];
    const up = Math.abs(pitch) > 1.5 ? [0, 1, 0] : [0, 0, 1];
    const view = lookAt(eye, target, up);
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    const r = this.bounds?.radius || 1;
    const proj = perspective(FOV, w / h, Math.max(dist - r * 4, dist * 0.01), dist + r * 8);
    return { view, mvp: multiply(proj, view), eye };
  }

  loop() {
    let last = performance.now();
    const tick = (now) => {
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      if (this.goal) {
        // critically damped approach towards the preset; any drag cancels it
        const k = 1 - Math.exp(-dt / 0.09);
        let dy = this.goal.yaw - this.cam.yaw;
        dy = Math.atan2(Math.sin(dy), Math.cos(dy));
        this.cam.yaw += dy * k;
        this.cam.pitch += (this.goal.pitch - this.cam.pitch) * k;
        if (Math.abs(dy) < 1e-3 && Math.abs(this.goal.pitch - this.cam.pitch) < 1e-3) this.goal = null;
        this.dirty = true;
      }
      if (this.dirty) { this.dirty = false; this.draw(); }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  draw() {
    const gl = this.gl, cv = this.canvas, dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(cv.clientWidth * dpr)), h = Math.max(1, Math.round(cv.clientHeight * dpr));
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    gl.viewport(0, 0, w, h);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    if (!this.bounds) { this.drawGizmo(null); return; }
    const { mvp, eye, view } = this.matrices();
    this.lastMvp = mvp;

    // grid
    this.useLines(mvp, 'gridPos', 'gridCol', this.gridCount, 0.35);

    // points first (opaque), then the mesh so a translucent mesh shows the nodes inside
    if (this.points && this.show.points) {
      const { p, loc } = this.prog.pts;
      gl.useProgram(p);
      gl.uniformMatrix4fv(loc.uMVP, false, mvp);
      gl.uniform1f(loc.uSize, this.pointSize * dpr);
      gl.uniform1f(loc.uMinVisible, this.minVisible);
      gl.uniform2f(loc.uRange, this.colorRange[0], this.colorRange[1]);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.lut);
      gl.uniform1i(loc.uLut, 0);
      this.attrib(loc.aPos, 'ptsPos', 3);
      this.attrib(loc.aT, 'ptsT', 1);
      gl.drawArrays(gl.POINTS, 0, this.points.count);
      gl.disableVertexAttribArray(loc.aT);
      gl.disableVertexAttribArray(loc.aPos);
    }

    if (this.mesh && this.show.mesh) {
      const { p, loc } = this.prog.mesh;
      gl.useProgram(p);
      gl.uniformMatrix4fv(loc.uMVP, false, mvp);
      gl.uniform3fv(loc.uLight, norm(sub(eye, this.cam.target)));
      gl.uniform3fv(loc.uBase, [0.64, 0.7, 0.8]);
      gl.uniform1f(loc.uAlpha, this.meshAlpha);
      if (this.meshAlpha < 1) gl.depthMask(false);
      this.attrib(loc.aPos, 'meshPos', 3);
      this.attrib(loc.aNormal, 'meshNrm', 3);
      gl.drawArrays(gl.TRIANGLES, 0, this.mesh.count);
      gl.disableVertexAttribArray(loc.aNormal);
      gl.disableVertexAttribArray(loc.aPos);
      gl.depthMask(true);
      if (this.show.wire && this.mesh.wireCount) this.useLines(mvp, 'wirePos', 'wireCol', this.mesh.wireCount, 0.55);
    }

    // hovered / picked node rings, always on top
    gl.disable(gl.DEPTH_TEST);
    for (const [idx, buf] of [[this.hovered, 'hoverPos'], [this.picked, 'pickPos']]) {
      if (idx < 0 || !this.points) continue;
      if (buf === 'hoverPos') this.upload('hoverPos', this.points.xyz.slice(3 * idx, 3 * idx + 3));
      const { p, loc } = this.prog.pick;
      gl.useProgram(p);
      gl.uniformMatrix4fv(loc.uMVP, false, mvp);
      this.attrib(loc.aPos, buf, 3);
      gl.uniform3fv(loc.uColor, [0, 0, 0]);
      gl.uniform1f(loc.uSize, (this.pointSize + 9) * dpr);
      gl.drawArrays(gl.POINTS, 0, 1);
      gl.uniform3fv(loc.uColor, buf === 'pickPos' ? [1, 1, 1] : [0.3, 0.75, 1]);
      gl.uniform1f(loc.uSize, (this.pointSize + 5) * dpr);
      gl.drawArrays(gl.POINTS, 0, 1);
      gl.disableVertexAttribArray(loc.aPos);
    }
    gl.enable(gl.DEPTH_TEST);
    this.drawGizmo(view);
  }

  useLines(mvp, posBuf, colBuf, count, alpha) {
    if (!count) return;
    const gl = this.gl, { p, loc } = this.prog.line;
    gl.useProgram(p);
    gl.uniformMatrix4fv(loc.uMVP, false, mvp);
    gl.uniform1f(loc.uAlpha, alpha);
    this.attrib(loc.aPos, posBuf, 3);
    this.attrib(loc.aColor, colBuf, 3);
    gl.drawArrays(gl.LINES, 0, count);
    gl.disableVertexAttribArray(loc.aColor);
    gl.disableVertexAttribArray(loc.aPos);
  }

  attrib(loc, buffer, size) {
    if (loc === undefined || loc < 0) return;
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buf[buffer]);
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
  }

  drawGizmo(view) {
    const cv = this.gizmo;
    if (!cv) return;
    const dpr = window.devicePixelRatio || 1, S = cv.clientWidth || 84;
    if (cv.width !== S * dpr) { cv.width = S * dpr; cv.height = S * dpr; }
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, S, S);
    if (!view) return;
    const c = S / 2, L = S * 0.34;
    const axes = [['x', '#e5484d', 0], ['y', '#30a46c', 1], ['z', '#3e7bfa', 2]]
      .map(([n, col, i]) => ({ n, col, x: view[4 * i], y: view[4 * i + 1], z: view[4 * i + 2] }))
      .sort((a, b) => a.z - b.z);
    g.font = '600 11px system-ui, sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    for (const a of axes) {
      const ex = c + a.x * L, ey = c - a.y * L;
      g.strokeStyle = a.col; g.lineWidth = 2.2; g.lineCap = 'round';
      g.beginPath(); g.moveTo(c, c); g.lineTo(ex, ey); g.stroke();
      g.fillStyle = a.col;
      g.beginPath(); g.arc(c + a.x * (L + 9), c - a.y * (L + 9), 7.5, 0, 2 * Math.PI); g.fill();
      g.fillStyle = '#fff';
      g.fillText(a.n, c + a.x * (L + 9), c - a.y * (L + 9) + 0.5);
    }
  }

  // nearest visible projected node within `radius` css px
  pick(cssX, cssY, radius = 12) {
    if (!this.points || !this.lastMvp || !this.show.points) return -1;
    const m = this.lastMvp, xyz = this.points.xyz, t = this.points.norm01;
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    let best = -1, bestD = radius * radius, bestZ = Infinity;
    for (let i = 0; i < this.points.count; i++) {
      if (t[i] < this.minVisible) continue;
      const x = xyz[3 * i], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
      const cw = m[3] * x + m[7] * y + m[11] * z + m[15];
      if (cw <= 0) continue;
      const sx = ((m[0] * x + m[4] * y + m[8] * z + m[12]) / cw + 1) * 0.5 * w;
      const sy = (1 - (m[1] * x + m[5] * y + m[9] * z + m[13]) / cw) * 0.5 * h;
      const sz = (m[2] * x + m[6] * y + m[10] * z + m[14]) / cw;
      const d = (sx - cssX) ** 2 + (sy - cssY) ** 2;
      if (d < bestD - 4 || (d < bestD + 4 && sz < bestZ)) { best = i; bestD = Math.min(d, bestD); bestZ = sz; }
    }
    return best;
  }

  bindInput() {
    const cv = this.canvas;
    let drag = null, hoverQueued = false, lastMove = null;
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    cv.addEventListener('pointerdown', (e) => {
      try { cv.setPointerCapture(e.pointerId); } catch { /* synthetic pointer */ }
      this.goal = null;  // grabbing interrupts any view animation
      drag = { x: e.clientX, y: e.clientY, sx: e.clientX, sy: e.clientY, pan: e.button === 2 || e.button === 1 || e.shiftKey };
    });
    cv.addEventListener('pointermove', (e) => {
      if (!drag) {
        lastMove = e;
        if (!hoverQueued && this.onHover && this.points && this.points.count <= 400000) {
          hoverQueued = true;
          requestAnimationFrame(() => {
            hoverQueued = false;
            const r = cv.getBoundingClientRect();
            const i = this.pick(lastMove.clientX - r.left, lastMove.clientY - r.top, 10);
            if (i !== this.hovered) { this.hovered = i; this.dirty = true; }
            this.onHover(i, lastMove.clientX - r.left, lastMove.clientY - r.top);
          });
        }
        return;
      }
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY;
      if (drag.pan) {
        const { view } = this.matrices();
        const right = [view[0], view[4], view[8]], up = [view[1], view[5], view[9]];
        const wpp = (2 * this.cam.dist * Math.tan(FOV / 2)) / (cv.clientHeight || 1);
        for (let a = 0; a < 3; a++) this.cam.target[a] += (-dx * right[a] + dy * up[a]) * wpp;
      } else {
        this.cam.yaw -= dx * 0.008;
        this.cam.pitch = Math.max(-1.56, Math.min(1.56, this.cam.pitch + dy * 0.008));
      }
      this.dirty = true;
    });
    cv.addEventListener('pointerup', (e) => {
      if (drag && Math.hypot(e.clientX - drag.sx, e.clientY - drag.sy) < 4 && !drag.pan && this.onPick) {
        const r = cv.getBoundingClientRect();
        const i = this.pick(e.clientX - r.left, e.clientY - r.top);
        this.setPicked(i);
        this.onPick(i);
      }
      drag = null;
    });
    cv.addEventListener('pointerleave', () => {
      if (this.hovered >= 0) { this.hovered = -1; this.dirty = true; }
      this.onHover?.(-1, 0, 0);
    });
    cv.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (!this.bounds) return;
      const r = this.bounds.radius;
      this.cam.dist = Math.max(r * 0.05, Math.min(r * 60, this.cam.dist * Math.exp(e.deltaY * 0.0012)));
      this.dirty = true;
    }, { passive: false });
  }
}
