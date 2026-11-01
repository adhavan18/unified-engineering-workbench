// canvas plots: labelled axes with units, log scales, legend (click to hide a
// series), drag-box zoom (double-click resets), hover readout, a vertical
// marker, and a bar chart with min–max whiskers. png export composites an
// opaque background so the image reads well outside the app.

export const PALETTE = ['#e5484d', '#f76b15', '#30a46c', '#2563eb', '#8e4ec6', '#0894b3', '#d6409f', '#946800'];

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const FONT = '12px "Segoe UI", system-ui, sans-serif';

function niceTicks(lo, hi, count = 6) {
  const span = hi - lo || 1, raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw) || 10 * mag;
  const out = [];
  for (let v = Math.ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step) out.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  return out;
}

function logTicks(lo, hi) {
  const e0 = Math.floor(Math.log10(lo)), e1 = Math.ceil(Math.log10(hi));
  const stride = Math.max(1, Math.ceil((e1 - e0) / 7));
  const out = [];
  for (let e = e0; e <= e1; e += stride) out.push(10 ** e);
  return out;
}

export function fmtTick(v) {
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a < 1e-3 || a >= 1e5) return v.toExponential(0).replace('e+', 'e');
  return String(Number(v.toPrecision(4)));
}

function setupCanvas(cv) {
  const dpr = window.devicePixelRatio || 1;
  const w = cv.clientWidth, h = cv.clientHeight;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.max(1, Math.round(w * dpr));
    cv.height = Math.max(1, Math.round(h * dpr));
  }
  const g = cv.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  g.font = FONT;
  return { g, w, h };
}

export function downloadCanvasPng(canvas, filename) {
  const out = document.createElement('canvas');
  out.width = canvas.width;
  out.height = canvas.height;
  const g = out.getContext('2d');
  g.fillStyle = css('--surface') || '#fff';
  g.fillRect(0, 0, out.width, out.height);
  g.drawImage(canvas, 0, 0);
  const a = document.createElement('a');
  a.href = out.toDataURL('image/png');
  a.download = filename;
  a.click();
}

export class LinePlot {
  constructor(canvas, { xLabel = '', yLabel = '', logX = false, logY = false, xUnit = '', yUnit = '', yFormat } = {}) {
    Object.assign(this, { canvas, xLabel, yLabel, logX, logY, xUnit, yUnit, yFormat });
    this.series = [];
    this.hidden = new Set();
    this.zoom = null;
    this.marker = null;
    this.hover = null;
    this.legendBoxes = [];
    new ResizeObserver(() => this.draw()).observe(canvas);
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => this.draw());
    this.bindInput();
  }

  // series: [{ name, x, y, color?, dash?, markers?, hollow?, noLine?, width? }]
  setSeries(series, { keepZoom = false } = {}) {
    this.series = series.map((s, i) => ({ color: PALETTE[i % PALETTE.length], ...s }));
    if (!keepZoom) this.zoom = null;
    this.draw();
  }

  setMarker(x, label = '') { this.marker = x === null ? null : { x, label }; this.draw(); }

  visible() { return this.series.filter((s) => !this.hidden.has(s.name)); }
  usable(x, y) { return Number.isFinite(x) && Number.isFinite(y) && (!this.logX || x > 0) && (!this.logY || y > 0); }

  bounds() {
    if (this.zoom) return this.zoom;
    let xlo = Infinity, xhi = -Infinity, ylo = Infinity, yhi = -Infinity;
    for (const s of this.visible()) for (let i = 0; i < s.x.length; i++) {
      if (!this.usable(s.x[i], s.y[i])) continue;
      if (s.x[i] < xlo) xlo = s.x[i]; if (s.x[i] > xhi) xhi = s.x[i];
      if (s.y[i] < ylo) ylo = s.y[i]; if (s.y[i] > yhi) yhi = s.y[i];
    }
    if (!Number.isFinite(xlo)) return null;
    if (this.logX) { xlo = 10 ** Math.floor(Math.log10(xlo)); xhi = 10 ** Math.ceil(Math.log10(xhi)); }
    if (this.logY) { ylo = 10 ** Math.floor(Math.log10(ylo)); yhi = 10 ** Math.ceil(Math.log10(yhi)); if (ylo === yhi) yhi *= 10; }
    else { const pad = (yhi - ylo) * 0.07 || Math.abs(yhi) * 0.1 || 1; ylo -= pad; yhi += pad; }
    if (xlo === xhi) { xlo -= 1; xhi += 1; }
    return { xlo, xhi, ylo, yhi };
  }

  layout() {
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    const legendW = this.series.length > 1 ? Math.min(150, Math.max(110, w * 0.2)) : 0;
    return { w, h, left: 62, right: legendW + 12, top: 14, bottom: 44 };
  }

  scales(b, L) {
    const pw = L.w - L.left - L.right, ph = L.h - L.top - L.bottom;
    const lx = (v) => (this.logX ? Math.log10(v) : v), ly = (v) => (this.logY ? Math.log10(v) : v);
    const X = (v) => L.left + ((lx(v) - lx(b.xlo)) / (lx(b.xhi) - lx(b.xlo))) * pw;
    const Y = (v) => L.top + (1 - (ly(v) - ly(b.ylo)) / (ly(b.yhi) - ly(b.ylo))) * ph;
    const invX = (px) => { const t = lx(b.xlo) + ((px - L.left) / pw) * (lx(b.xhi) - lx(b.xlo)); return this.logX ? 10 ** t : t; };
    const invY = (py) => { const t = ly(b.ylo) + (1 - (py - L.top) / ph) * (ly(b.yhi) - ly(b.ylo)); return this.logY ? 10 ** t : t; };
    return { X, Y, invX, invY, pw, ph };
  }

  draw() {
    const { g, w, h } = setupCanvas(this.canvas);
    const L = this.layout();
    const text = css('--text'), muted = css('--muted'), grid = css('--plot-grid'), axis = css('--plot-axis');
    const b = this.bounds();
    if (!b) {
      g.fillStyle = muted; g.textAlign = 'center';
      g.fillText(this.series.length ? 'all series hidden: click the legend' : 'waiting for data…', w / 2, h / 2);
      this.drawLegend(g, L, text, muted);
      return;
    }
    const { X, Y, pw, ph } = this.scales(b, L);
    this.cache = { b, L };

    g.lineWidth = 1;
    g.strokeStyle = grid;
    g.fillStyle = muted;
    g.textAlign = 'center';
    for (const v of this.logX ? logTicks(b.xlo, b.xhi) : niceTicks(b.xlo, b.xhi)) {
      if (v < b.xlo * (1 - 1e-9) - 1e-15 || v > b.xhi * (1 + 1e-9) + 1e-15) continue;
      g.beginPath(); g.moveTo(X(v), L.top); g.lineTo(X(v), L.top + ph); g.stroke();
      g.fillText(fmtTick(v), X(v), L.top + ph + 16);
    }
    g.textAlign = 'right';
    for (const v of this.logY ? logTicks(b.ylo, b.yhi) : niceTicks(b.ylo, b.yhi, 5)) {
      if (v < b.ylo - 1e-15 || v > b.yhi + 1e-15) continue;
      g.beginPath(); g.moveTo(L.left, Y(v)); g.lineTo(L.left + pw, Y(v)); g.stroke();
      g.fillText(this.yFormat ? this.yFormat(v) : fmtTick(v), L.left - 7, Y(v) + 4);
    }
    g.strokeStyle = axis;
    g.strokeRect(L.left + 0.5, L.top + 0.5, pw, ph);
    g.fillStyle = text;
    g.textAlign = 'center';
    g.fillText(this.xLabel, L.left + pw / 2, h - 8);
    g.save(); g.translate(13, L.top + ph / 2); g.rotate(-Math.PI / 2); g.fillText(this.yLabel, 0, 0); g.restore();

    g.save();
    g.beginPath(); g.rect(L.left, L.top, pw, ph); g.clip();
    if (this.marker) {
      g.strokeStyle = css('--accent'); g.lineWidth = 1.5; g.setLineDash([4, 4]);
      g.beginPath(); g.moveTo(X(this.marker.x), L.top); g.lineTo(X(this.marker.x), L.top + ph); g.stroke();
      g.setLineDash([]);
    }
    for (const s of this.visible()) {
      g.strokeStyle = s.color; g.fillStyle = s.color; g.lineWidth = s.width || 2;
      g.setLineDash(s.dash ? [6, 4] : []);
      if (!s.noLine) {
        g.beginPath();
        let started = false;
        for (let i = 0; i < s.x.length; i++) {
          if (!this.usable(s.x[i], s.y[i])) { started = false; continue; }
          if (!started) { g.moveTo(X(s.x[i]), Y(s.y[i])); started = true; } else g.lineTo(X(s.x[i]), Y(s.y[i]));
        }
        g.stroke();
      }
      g.setLineDash([]);
      if (s.markers) for (let i = 0; i < s.x.length; i++) {
        if (!this.usable(s.x[i], s.y[i])) continue;
        g.beginPath(); g.arc(X(s.x[i]), Y(s.y[i]), s.markerSize || 3.4, 0, 2 * Math.PI);
        if (s.hollow) { g.lineWidth = 1.6; g.stroke(); } else g.fill();
      }
    }
    if (this.box) {
      g.fillStyle = 'rgba(37,99,235,0.12)'; g.strokeStyle = css('--accent'); g.lineWidth = 1;
      const x = Math.min(this.box.x0, this.box.x1), y = Math.min(this.box.y0, this.box.y1);
      g.fillRect(x, y, Math.abs(this.box.x1 - this.box.x0), Math.abs(this.box.y1 - this.box.y0));
      g.strokeRect(x, y, Math.abs(this.box.x1 - this.box.x0), Math.abs(this.box.y1 - this.box.y0));
    }
    g.restore();

    this.drawLegend(g, L, text, muted);
    if (this.zoom) { g.fillStyle = muted; g.textAlign = 'left'; g.fillText('zoomed · double-click to reset', L.left + 6, L.top + 14); }

    if (this.hover && !this.hidden.has(this.hover.s.name)) {
      const { s, i } = this.hover;
      const px = X(s.x[i]), py = Y(s.y[i]);
      g.fillStyle = s.color; g.strokeStyle = css('--surface'); g.lineWidth = 2;
      g.beginPath(); g.arc(px, py, 5, 0, 2 * Math.PI); g.fill(); g.stroke();
      const yv = Math.abs(s.y[i]) < 1e-3 && s.y[i] !== 0 ? s.y[i].toExponential(3) : Number(s.y[i].toPrecision(6));
      const msg = `${s.name}   ${this.xLabel.split(' (')[0]} = ${fmtTick(s.x[i])}${this.xUnit}   →   ${yv}${this.yUnit}`;
      const tw = g.measureText(msg).width + 16;
      const bx = Math.min(Math.max(px + 12, L.left), w - tw - 4), by = Math.max(py - 34, 2);
      g.fillStyle = css('--surface'); g.strokeStyle = css('--border-strong'); g.lineWidth = 1;
      g.beginPath(); g.roundRect(bx, by, tw, 24, 6); g.fill(); g.stroke();
      g.fillStyle = text; g.textAlign = 'left';
      g.fillText(msg, bx + 8, by + 16);
    }
  }

  drawLegend(g, L, text, muted) {
    this.legendBoxes = [];
    if (this.series.length < 2) return;
    let ly = L.top + 8;
    const lx = L.w - L.right + 14;
    g.textAlign = 'left';
    for (const s of this.series) {
      const off = this.hidden.has(s.name);
      g.globalAlpha = off ? 0.35 : 1;
      g.strokeStyle = s.color; g.fillStyle = s.color; g.lineWidth = 2;
      g.setLineDash(s.dash ? [6, 4] : []);
      if (!s.noLine) { g.beginPath(); g.moveTo(lx, ly); g.lineTo(lx + 20, ly); g.stroke(); }
      g.setLineDash([]);
      if (s.markers || s.noLine) { g.beginPath(); g.arc(lx + 10, ly, 3.4, 0, 2 * Math.PI); if (s.hollow) g.stroke(); else g.fill(); }
      g.fillStyle = off ? muted : text;
      g.fillText(s.name, lx + 26, ly + 4);
      if (off) { g.strokeStyle = muted; g.lineWidth = 1; g.beginPath(); g.moveTo(lx + 26, ly); g.lineTo(lx + 26 + g.measureText(s.name).width, ly); g.stroke(); }
      this.legendBoxes.push({ name: s.name, x: lx - 4, y: ly - 9, w: L.right - 10, h: 18 });
      ly += 19;
    }
    g.globalAlpha = 1;
  }

  bindInput() {
    const cv = this.canvas;
    const pos = (e) => { const r = cv.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
    cv.addEventListener('pointerdown', (e) => {
      const p = pos(e);
      const hit = this.legendBoxes.find((b) => p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h);
      if (hit) {
        if (this.hidden.has(hit.name)) this.hidden.delete(hit.name); else this.hidden.add(hit.name);
        this.zoom = null;
        this.draw();
        return;
      }
      if (!this.cache) return;
      const { L } = this.cache;
      if (p.x < L.left || p.x > L.w - L.right || p.y < L.top || p.y > L.h - L.bottom) return;
      try { cv.setPointerCapture(e.pointerId); } catch { /* ignore */ }
      this.box = { x0: p.x, y0: p.y, x1: p.x, y1: p.y };
    });
    cv.addEventListener('pointermove', (e) => {
      const p = pos(e);
      if (this.box) { this.box.x1 = p.x; this.box.y1 = p.y; this.draw(); return; }
      this.onHover(p);
      cv.style.cursor = this.legendBoxes.some((b) => p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h) ? 'pointer' : 'crosshair';
    });
    cv.addEventListener('pointerup', () => {
      const bx = this.box;
      this.box = null;
      if (!bx || !this.cache) return;
      if (Math.abs(bx.x1 - bx.x0) > 8 && Math.abs(bx.y1 - bx.y0) > 8) {
        const { invX, invY } = this.scales(this.cache.b, this.cache.L);
        const xs = [invX(bx.x0), invX(bx.x1)].sort((a, c) => a - c), ys = [invY(bx.y0), invY(bx.y1)].sort((a, c) => a - c);
        this.zoom = { xlo: xs[0], xhi: xs[1], ylo: ys[0], yhi: ys[1] };
      }
      this.draw();
    });
    cv.addEventListener('dblclick', () => { this.zoom = null; this.draw(); });
    cv.addEventListener('pointerleave', () => { this.hover = null; this.draw(); });
  }

  onHover(p) {
    if (!this.cache) return;
    const { X, Y } = this.scales(this.cache.b, this.cache.L);
    let best = null, bestD = 500;
    for (const s of this.visible()) for (let i = 0; i < s.x.length; i++) {
      if (!this.usable(s.x[i], s.y[i])) continue;
      const d = (X(s.x[i]) - p.x) ** 2 + (Y(s.y[i]) - p.y) ** 2;
      if (d < bestD) { bestD = d; best = { s, i }; }
    }
    if (best?.s !== this.hover?.s || best?.i !== this.hover?.i) { this.hover = best; this.draw(); }
  }

  downloadPng(filename) { downloadCanvasPng(this.canvas, filename); }
}

// vertical bars with optional min–max whiskers; one bar per category.
export class BarChart {
  constructor(canvas, { yLabel = '', yUnit = '' } = {}) {
    Object.assign(this, { canvas, yLabel, yUnit });
    this.bars = [];
    new ResizeObserver(() => this.draw()).observe(canvas);
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => this.draw());
  }

  // bars: [{ label, value, lo?, hi?, color?, note? }]
  setBars(bars) { this.bars = bars; this.draw(); }

  draw() {
    const { g, w, h } = setupCanvas(this.canvas);
    const text = css('--text'), muted = css('--muted'), grid = css('--plot-grid'), axis = css('--plot-axis');
    if (!this.bars.length) { g.fillStyle = muted; g.textAlign = 'center'; g.fillText('waiting for data…', w / 2, h / 2); return; }
    const L = { left: 58, right: 12, top: 22, bottom: 40 };
    const pw = w - L.left - L.right, ph = h - L.top - L.bottom;
    const top = Math.max(...this.bars.map((b) => b.hi ?? b.value)) * 1.12 || 1;
    const Y = (v) => L.top + (1 - v / top) * ph;
    g.strokeStyle = grid; g.fillStyle = muted; g.textAlign = 'right'; g.lineWidth = 1;
    for (const v of niceTicks(0, top, 5)) {
      if (v > top) continue;
      g.beginPath(); g.moveTo(L.left, Y(v)); g.lineTo(L.left + pw, Y(v)); g.stroke();
      g.fillText(fmtTick(v), L.left - 7, Y(v) + 4);
    }
    g.strokeStyle = axis; g.beginPath(); g.moveTo(L.left, L.top + ph + 0.5); g.lineTo(L.left + pw, L.top + ph + 0.5); g.stroke();
    const slot = pw / this.bars.length, bw = Math.min(64, slot * 0.55);
    this.bars.forEach((b, i) => {
      const cx = L.left + slot * (i + 0.5);
      g.fillStyle = b.color || PALETTE[i % PALETTE.length];
      g.beginPath(); g.roundRect(cx - bw / 2, Y(b.value), bw, L.top + ph - Y(b.value), [6, 6, 0, 0]); g.fill();
      if (b.lo !== undefined && b.hi !== undefined) {
        g.strokeStyle = text; g.lineWidth = 1.4;
        g.beginPath(); g.moveTo(cx, Y(b.lo)); g.lineTo(cx, Y(b.hi));
        g.moveTo(cx - 7, Y(b.lo)); g.lineTo(cx + 7, Y(b.lo)); g.moveTo(cx - 7, Y(b.hi)); g.lineTo(cx + 7, Y(b.hi)); g.stroke();
      }
      g.fillStyle = text; g.textAlign = 'center';
      g.font = '600 12px "Segoe UI", system-ui, sans-serif';
      g.fillText(`${fmtTick(b.value)}${this.yUnit}`, cx, Math.min(Y(b.hi ?? b.value), Y(b.value)) - 7);
      g.font = FONT;
      g.fillStyle = muted;
      g.fillText(b.label, cx, L.top + ph + 17);
      if (b.note) g.fillText(b.note, cx, L.top + ph + 32);
    });
    g.save(); g.translate(13, L.top + ph / 2); g.rotate(-Math.PI / 2); g.fillStyle = text; g.textAlign = 'center'; g.fillText(this.yLabel, 0, 0); g.restore();
  }

  downloadPng(filename) { downloadCanvasPng(this.canvas, filename); }
}

// tiny inline trend line for telemetry tables
export function sparkline(canvas, values, color) {
  const { g, w, h } = setupCanvas(canvas);
  if (values.length < 2) return;
  let lo = Math.min(...values), hi = Math.max(...values);
  if (hi - lo < 1e-12) { lo -= 1; hi += 1; }
  g.strokeStyle = color || css('--accent'); g.lineWidth = 1.5;
  g.beginPath();
  values.forEach((v, i) => {
    const x = (i / (values.length - 1)) * (w - 2) + 1, y = h - 2 - ((v - lo) / (hi - lo)) * (h - 4);
    if (i) g.lineTo(x, y); else g.moveTo(x, y);
  });
  g.stroke();
}
