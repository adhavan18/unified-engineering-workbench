// small webgl viewer for stl triangles and coloured point clouds.
// orbit (left drag), pan (right drag or shift+left), zoom (wheel), fit to view,
// and point picking for node inspection. z is up, as in most cad tools.

const FOV = (40 * Math.PI) / 180;

const VS_MESH = `
attribute vec3 aPos; attribute vec3 aNormal;
uniform mat4 uMVP; varying vec3 vN;
void main() { vN = aNormal; gl_Position = uMVP * vec4(aPos, 1.0); }`;
const FS_MESH = `
precision mediump float; varying vec3 vN; uniform vec3 uLight; uniform vec3 uBase;
void main() {
  float d = abs(dot(normalize(vN), uLight));          // two-sided headlight
  gl_FragColor = vec4(uBase * (0.28 + 0.72 * d), 1.0);
}`;
const VS_PTS = `
attribute vec3 aPos; attribute vec3 aColor;
uniform mat4 uMVP; uniform float uSize; varying vec3 vC;
void main() { vC = aColor; gl_Position = uMVP * vec4(aPos, 1.0); gl_PointSize = uSize; }`;
const FS_PTS = `
precision mediump float; varying vec3 vC; uniform float uRound; uniform float uUseColor; uniform vec3 uColor;
void main() {
  if (uRound > 0.5 && length(gl_PointCoord - vec2(0.5)) > 0.5) discard;
  gl_FragColor = vec4(mix(vC, uColor, uUseColor), 1.0);
}`;

// ---- tiny column-major mat4 helpers ----
function perspective(fovy, aspect, near, far) {
  const f = 1 / Math.tan(fovy / 2);
  const m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = (far + near) / (near - far); m[11] = -1; m[14] = (2 * far * near) / (near - far);
  return m;
}
function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function norm(a) { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
function lookAt(eye, center, up) {
  const z = norm(sub(eye, center)), x = norm(cross(up, z)), y = cross(z, x);
  return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
    -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]);
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
  return p;
}

export class Viewer {
  constructor(canvas, { onPick } = {}) {
    this.canvas = canvas;
    this.onPick = onPick;
    const gl = canvas.getContext('webgl', { antialias: true, preserveDrawingBuffer: true });
    if (!gl) throw new Error('webgl is not available in this browser');
    this.gl = gl;
    this.meshProg = compile(gl, VS_MESH, FS_MESH);
    this.ptsProg = compile(gl, VS_PTS, FS_PTS);
    this.buffers = {};
    this.mesh = null;     // { count }
    this.points = null;   // { count, xyz }
    this.picked = -1;
    this.pointSize = 6;
    this.bbox = null;
    this.cam = { yaw: -Math.PI / 3, pitch: Math.PI / 6, dist: 10, target: [0, 0, 0] };
    this.dirty = true;
    this.bindInput();
    new ResizeObserver(() => this.requestDraw()).observe(canvas);
    this.loop();
  }

  // positions: Float32Array (9 per triangle); faceNormals: Float32Array (3 per triangle)
  setMesh(positions, faceNormals, bboxMin, bboxMax) {
    const gl = this.gl;
    const tris = positions.length / 9;
    const normals = new Float32Array(positions.length);
    for (let t = 0; t < tris; t++) for (let v = 0; v < 3; v++) normals.set(faceNormals.subarray(3 * t, 3 * t + 3), 9 * t + 3 * v);
    this.upload('meshPos', positions);
    this.upload('meshNrm', normals);
    this.mesh = { count: tris * 3 };
    this.setBox(bboxMin, bboxMax);
    this.fit();
  }

  // xyz: Float32Array (3 per point); colors: Float32Array (rgb 0..1, 3 per point)
  setPoints(xyz, colors, bboxMin, bboxMax) {
    this.upload('ptsPos', xyz);
    this.upload('ptsCol', colors);
    this.points = { count: xyz.length / 3, xyz };
    this.picked = -1;
    this.setBox(bboxMin, bboxMax);
    this.fit();
  }

  setPointSize(px) { this.pointSize = px; this.requestDraw(); }

  setPicked(i) {
    this.picked = i;
    if (i >= 0) this.upload('pickPos', this.points.xyz.slice(3 * i, 3 * i + 3));
    this.requestDraw();
  }

  fit() {
    if (!this.bbox) return;
    const { center, radius } = this.bbox;
    this.cam.target = center.slice();
    this.cam.dist = radius / Math.sin(FOV / 2) * 1.08;
    this.requestDraw();
  }

  reset() {
    this.cam.yaw = -Math.PI / 3;
    this.cam.pitch = Math.PI / 6;
    this.fit();
  }

  // ---- internals ----
  upload(name, data) {
    const gl = this.gl;
    if (!this.buffers[name]) this.buffers[name] = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers[name]);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  }

  setBox(min, max) {
    const center = [0, 1, 2].map((a) => (min[a] + max[a]) / 2);
    const size = [0, 1, 2].map((a) => max[a] - min[a]);
    const radius = Math.max(Math.hypot(...size) / 2, 1e-9);
    this.bbox = { min, max, center, radius };
    // bounding-box edges (grey) and an xyz axis triad at the min corner (red/green/blue)
    const [x0, y0, z0] = min, [x1, y1, z1] = max;
    const c = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
    const edges = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
    const pos = [], col = [];
    for (const [a, b] of edges) { pos.push(...c[a], ...c[b]); col.push(0.55, 0.57, 0.62, 0.55, 0.57, 0.62); }
    const L = radius * 0.35;
    const axes = [[[L, 0, 0], [0.86, 0.15, 0.15]], [[0, L, 0], [0.13, 0.65, 0.3]], [[0, 0, L], [0.15, 0.39, 0.92]]];
    for (const [d, rgb] of axes) {
      pos.push(x0, y0, z0, x0 + d[0], y0 + d[1], z0 + d[2]);
      col.push(...rgb, ...rgb);
    }
    this.upload('linePos', new Float32Array(pos));
    this.upload('lineCol', new Float32Array(col));
    this.lineCount = pos.length / 3;
  }

  matrices() {
    const { yaw, pitch, dist, target } = this.cam;
    const eye = [target[0] + dist * Math.cos(pitch) * Math.cos(yaw),
      target[1] + dist * Math.cos(pitch) * Math.sin(yaw),
      target[2] + dist * Math.sin(pitch)];
    const view = lookAt(eye, target, [0, 0, 1]);
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    const proj = perspective(FOV, w / h, dist * 0.01, dist * 100 + (this.bbox?.radius || 1) * 4);
    return { view, mvp: multiply(proj, view), eye };
  }

  requestDraw() { this.dirty = true; }

  loop() {
    const tick = () => {
      if (this.dirty) { this.dirty = false; this.draw(); }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  draw() {
    const gl = this.gl, cv = this.canvas;
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(cv.clientWidth * dpr)), h = Math.max(1, Math.round(cv.clientHeight * dpr));
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    gl.viewport(0, 0, w, h);
    const bg = getComputedStyle(cv.parentElement).backgroundColor.match(/\d+(\.\d+)?/g)?.map(Number) || [238, 240, 244];
    gl.clearColor(bg[0] / 255, bg[1] / 255, bg[2] / 255, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    if (!this.bbox) return;

    const { mvp, eye } = this.matrices();
    this.lastMvp = mvp;

    if (this.mesh) {
      const p = this.meshProg;
      gl.useProgram(p);
      gl.uniformMatrix4fv(gl.getUniformLocation(p, 'uMVP'), false, mvp);
      gl.uniform3fv(gl.getUniformLocation(p, 'uLight'), norm(sub(eye, this.cam.target)));
      gl.uniform3fv(gl.getUniformLocation(p, 'uBase'), [0.62, 0.69, 0.8]);
      this.attrib(p, 'aPos', 'meshPos');
      this.attrib(p, 'aNormal', 'meshNrm');
      gl.drawArrays(gl.TRIANGLES, 0, this.mesh.count);
      this.disable(p, ['aPos', 'aNormal']);
    }

    const p = this.ptsProg;
    gl.useProgram(p);
    gl.uniformMatrix4fv(gl.getUniformLocation(p, 'uMVP'), false, mvp);
    const uRound = gl.getUniformLocation(p, 'uRound'), uUse = gl.getUniformLocation(p, 'uUseColor');
    const uColor = gl.getUniformLocation(p, 'uColor'), uSize = gl.getUniformLocation(p, 'uSize');

    gl.uniform1f(uRound, 0); gl.uniform1f(uUse, 0); gl.uniform1f(uSize, 1);
    this.attrib(p, 'aPos', 'linePos');
    this.attrib(p, 'aColor', 'lineCol');
    gl.drawArrays(gl.LINES, 0, this.lineCount);

    if (this.points) {
      gl.uniform1f(uRound, 1);
      gl.uniform1f(uSize, this.pointSize * dpr);
      this.attrib(p, 'aPos', 'ptsPos');
      this.attrib(p, 'aColor', 'ptsCol');
      gl.drawArrays(gl.POINTS, 0, this.points.count);
      if (this.picked >= 0) {
        gl.disable(gl.DEPTH_TEST);
        this.disable(p, ['aColor']);
        this.attrib(p, 'aPos', 'pickPos');
        gl.uniform1f(uUse, 1);
        gl.uniform3fv(uColor, [0, 0, 0]);
        gl.uniform1f(uSize, (this.pointSize + 10) * dpr);
        gl.drawArrays(gl.POINTS, 0, 1);
        gl.uniform3fv(uColor, [1, 1, 1]);
        gl.uniform1f(uSize, (this.pointSize + 5) * dpr);
        gl.drawArrays(gl.POINTS, 0, 1);
        gl.enable(gl.DEPTH_TEST);
      }
    }
    this.disable(p, ['aPos', 'aColor']);
  }

  attrib(prog, name, buffer) {
    const gl = this.gl;
    const loc = gl.getAttribLocation(prog, name);
    if (loc < 0) return;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers[buffer]);
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0);
  }

  disable(prog, names) {
    for (const n of names) {
      const loc = this.gl.getAttribLocation(prog, n);
      if (loc >= 0) this.gl.disableVertexAttribArray(loc);
    }
  }

  // nearest projected point within 12 css px of the click, front-most on ties
  pick(cssX, cssY) {
    if (!this.points || !this.lastMvp) return -1;
    const m = this.lastMvp, xyz = this.points.xyz;
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    let best = -1, bestD = 144, bestZ = Infinity;
    for (let i = 0; i < this.points.count; i++) {
      const x = xyz[3 * i], y = xyz[3 * i + 1], z = xyz[3 * i + 2];
      const cw = m[3] * x + m[7] * y + m[11] * z + m[15];
      if (cw <= 0) continue;
      const sx = ((m[0] * x + m[4] * y + m[8] * z + m[12]) / cw + 1) * 0.5 * w;
      const sy = (1 - (m[1] * x + m[5] * y + m[9] * z + m[13]) / cw) * 0.5 * h;
      const sz = (m[2] * x + m[6] * y + m[10] * z + m[14]) / cw;
      const d = (sx - cssX) ** 2 + (sy - cssY) ** 2;
      if (d < bestD - 1e-6 || (Math.abs(d - bestD) < 4 && sz < bestZ)) { best = i; bestD = d; bestZ = sz; }
    }
    return best;
  }

  bindInput() {
    const cv = this.canvas;
    let drag = null;
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    cv.addEventListener('pointerdown', (e) => {
      try { cv.setPointerCapture(e.pointerId); } catch { /* synthetic or already-released pointer */ }
      drag = { x: e.clientX, y: e.clientY, sx: e.clientX, sy: e.clientY, pan: e.button === 2 || e.shiftKey || e.button === 1 };
    });
    cv.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY;
      if (drag.pan) {
        const { view } = this.matrices();
        const right = [view[0], view[4], view[8]], up = [view[1], view[5], view[9]];
        const wpp = (2 * this.cam.dist * Math.tan(FOV / 2)) / (cv.clientHeight || 1);
        for (let a = 0; a < 3; a++) this.cam.target[a] += (-dx * right[a] + dy * up[a]) * wpp;
      } else {
        this.cam.yaw -= dx * 0.008;
        this.cam.pitch = Math.max(-1.55, Math.min(1.55, this.cam.pitch + dy * 0.008));
      }
      this.requestDraw();
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
    cv.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (!this.bbox) return;
      const r = this.bbox.radius;
      this.cam.dist = Math.max(r * 0.02, Math.min(r * 200, this.cam.dist * Math.exp(e.deltaY * 0.0015)));
      this.requestDraw();
    }, { passive: false });
  }
}

// viridis, sampled at 9 stops (matplotlib's perceptually uniform colormap)
const VIRIDIS = [[68, 1, 84], [71, 44, 122], [59, 81, 139], [44, 113, 142], [33, 144, 141],
  [39, 173, 129], [92, 200, 99], [170, 220, 50], [253, 231, 37]];

export function viridis(t) {
  const x = Math.min(1, Math.max(0, t)) * (VIRIDIS.length - 1);
  const i = Math.min(VIRIDIS.length - 2, Math.floor(x)), f = x - i;
  const a = VIRIDIS[i], b = VIRIDIS[i + 1];
  return [(a[0] + (b[0] - a[0]) * f) / 255, (a[1] + (b[1] - a[1]) * f) / 255, (a[2] + (b[2] - a[2]) * f) / 255];
}
