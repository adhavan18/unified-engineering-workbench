// canvas line plots with labelled axes (units), legend, log scales and a hover readout.

const PALETTE = ['#e5484d', '#f76b15', '#30a46c', '#0a66d8', '#8e4ec6', '#0894b3'];

function css(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function niceTicks(lo, hi, count = 6) {
  const span = hi - lo || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw) || 10 * mag;
  const out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) out.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  return out;
}

// one tick per decade, thinned to every 2nd/3rd/... decade when the range is wide
function logTicks(lo, hi) {
  const e0 = Math.floor(Math.log10(lo)), e1 = Math.ceil(Math.log10(hi));
  const stride = Math.max(1, Math.ceil((e1 - e0) / 7));
  const out = [];
  for (let e = e0; e <= e1; e += stride) out.push(10 ** e);
  return out;
}

function label(v) {
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a < 1e-3 || a >= 1e5) return v.toExponential(0).replace('e+', 'e');
  return String(Number(v.toPrecision(4)));
}

export class LinePlot {
  constructor(canvas, { xLabel = '', yLabel = '', logX = false, logY = false, xUnit = '', yUnit = '' } = {}) {
    Object.assign(this, { canvas, xLabel, yLabel, logX, logY, xUnit, yUnit });
    this.series = [];
    this.hover = null;
    new ResizeObserver(() => this.draw()).observe(canvas);
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => this.draw());
    canvas.addEventListener('pointermove', (e) => this.onHover(e));
    canvas.addEventListener('pointerleave', () => { this.hover = null; this.draw(); });
  }

  // series: [{ name, x: number[], y: number[], color?, dash?, markers? }]
  setSeries(series) {
    this.series = series.map((s, i) => ({ color: PALETTE[i % PALETTE.length], ...s }));
    this.draw();
  }

  usable(x, y) {
    return Number.isFinite(x) && Number.isFinite(y) && (!this.logX || x > 0) && (!this.logY || y > 0);
  }

  bounds() {
    let xlo = Infinity, xhi = -Infinity, ylo = Infinity, yhi = -Infinity;
    for (const s of this.series) for (let i = 0; i < s.x.length; i++) {
      if (!this.usable(s.x[i], s.y[i])) continue;
      xlo = Math.min(xlo, s.x[i]); xhi = Math.max(xhi, s.x[i]);
      ylo = Math.min(ylo, s.y[i]); yhi = Math.max(yhi, s.y[i]);
    }
    if (!Number.isFinite(xlo)) return null;
    if (this.logX) { xlo = 10 ** Math.floor(Math.log10(xlo)); xhi = 10 ** Math.ceil(Math.log10(xhi)); }
    if (this.logY) { ylo = 10 ** Math.floor(Math.log10(ylo)); yhi = 10 ** Math.ceil(Math.log10(yhi)); }
    else { const pad = (yhi - ylo) * 0.06 || Math.abs(yhi) * 0.1 || 1; ylo -= pad; yhi += pad; }
    if (xlo === xhi) { xlo -= 1; xhi += 1; }
    return { xlo, xhi, ylo, yhi };
  }

  layout() {
    const w = this.canvas.clientWidth, h = this.canvas.clientHeight;
    const legendW = this.series.length ? Math.min(190, w * 0.3) : 0;
    return { w, h, left: 64, right: legendW + 16, top: 12, bottom: 46 };
  }

  scales(b, L) {
    const pw = L.w - L.left - L.right, ph = L.h - L.top - L.bottom;
    const tx = (v) => (this.logX ? (Math.log10(v) - Math.log10(b.xlo)) / (Math.log10(b.xhi) - Math.log10(b.xlo)) : (v - b.xlo) / (b.xhi - b.xlo));
    const ty = (v) => (this.logY ? (Math.log10(v) - Math.log10(b.ylo)) / (Math.log10(b.yhi) - Math.log10(b.ylo)) : (v - b.ylo) / (b.yhi - b.ylo));
    return { X: (v) => L.left + tx(v) * pw, Y: (v) => L.top + (1 - ty(v)) * ph, pw, ph };
  }

  draw() {
    const cv = this.canvas, dpr = window.devicePixelRatio || 1;
    const L = this.layout();
    cv.width = Math.max(1, Math.round(L.w * dpr));
    cv.height = Math.max(1, Math.round(L.h * dpr));
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, L.w, L.h);
    const text = css('--text'), muted = css('--muted'), grid = css('--plot-grid'), axis = css('--plot-axis');
    g.font = '12px system-ui, "Segoe UI", sans-serif';

    const b = this.bounds();
    if (!b) {
      g.fillStyle = muted;
      g.textAlign = 'center';
      g.fillText('no data yet', L.w / 2, L.h / 2);
      return;
    }
    const { X, Y, pw, ph } = this.scales(b, L);

    g.strokeStyle = grid;
    g.lineWidth = 1;
    g.fillStyle = muted;
    g.textAlign = 'center';
    for (const v of this.logX ? logTicks(b.xlo, b.xhi) : niceTicks(b.xlo, b.xhi)) {
      if (v < b.xlo * (1 - 1e-9) - 1e-15 || v > b.xhi * (1 + 1e-9) + 1e-15) continue;
      g.beginPath(); g.moveTo(X(v), L.top); g.lineTo(X(v), L.top + ph); g.stroke();
      g.fillText(label(v), X(v), L.top + ph + 16);
    }
    g.textAlign = 'right';
    for (const v of this.logY ? logTicks(b.ylo, b.yhi) : niceTicks(b.ylo, b.yhi)) {
      if (v < b.ylo - 1e-15 || v > b.yhi + 1e-15) continue;
      g.beginPath(); g.moveTo(L.left, Y(v)); g.lineTo(L.left + pw, Y(v)); g.stroke();
      g.fillText(label(v), L.left - 6, Y(v) + 4);
    }
    g.strokeStyle = axis;
    g.strokeRect(L.left, L.top, pw, ph);

    g.fillStyle = text;
    g.textAlign = 'center';
    g.fillText(this.xLabel, L.left + pw / 2, L.h - 8);
    g.save();
    g.translate(14, L.top + ph / 2);
    g.rotate(-Math.PI / 2);
    g.fillText(this.yLabel, 0, 0);
    g.restore();

    g.save();
    g.beginPath(); g.rect(L.left, L.top, pw, ph); g.clip();
    for (const s of this.series) {
      g.strokeStyle = s.color;
      g.fillStyle = s.color;
      g.lineWidth = 2;
      g.setLineDash(s.dash ? [6, 4] : []);
      if (!s.noLine) {
        g.beginPath();
        let started = false;
        for (let i = 0; i < s.x.length; i++) {
          if (!this.usable(s.x[i], s.y[i])) continue;
          if (!started) { g.moveTo(X(s.x[i]), Y(s.y[i])); started = true; } else g.lineTo(X(s.x[i]), Y(s.y[i]));
        }
        g.stroke();
      }
      g.setLineDash([]);
      if (s.markers) for (let i = 0; i < s.x.length; i++) {
        if (!this.usable(s.x[i], s.y[i])) continue;
        g.beginPath(); g.arc(X(s.x[i]), Y(s.y[i]), 3.2, 0, 2 * Math.PI); g.fill();
      }
    }
    g.restore();

    // legend
    g.textAlign = 'left';
    let ly = L.top + 8;
    const lx = L.left + pw + 14;
    for (const s of this.series) {
      g.strokeStyle = s.color; g.fillStyle = s.color; g.lineWidth = 2;
      g.setLineDash(s.dash ? [6, 4] : []);
      if (!s.noLine) { g.beginPath(); g.moveTo(lx, ly); g.lineTo(lx + 22, ly); g.stroke(); }
      g.setLineDash([]);
      if (s.markers) { g.beginPath(); g.arc(lx + 11, ly, 3.2, 0, 2 * Math.PI); g.fill(); }
      g.fillStyle = text;
      g.fillText(s.name, lx + 28, ly + 4);
      ly += 18;
    }

    if (this.hover) {
      const { s, i } = this.hover;
      const px = X(s.x[i]), py = Y(s.y[i]);
      g.fillStyle = s.color;
      g.beginPath(); g.arc(px, py, 5, 0, 2 * Math.PI); g.fill();
      const msg = `${s.name}: ${label(s.x[i])}${this.xUnit} → ${Number(s.y[i].toPrecision(6))}${this.yUnit}`;
      const tw = g.measureText(msg).width + 14;
      const bx = Math.min(px + 10, L.w - tw - 4), by = Math.max(py - 30, 4);
      g.fillStyle = css('--surface');
      g.strokeStyle = css('--border');
      g.fillRect(bx, by, tw, 22); g.strokeRect(bx, by, tw, 22);
      g.fillStyle = text;
      g.fillText(msg, bx + 7, by + 15);
    }
  }

  onHover(e) {
    const b = this.bounds();
    if (!b) return;
    const r = this.canvas.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const { X, Y } = this.scales(b, this.layout());
    let best = null, bestD = 400;
    for (const s of this.series) for (let i = 0; i < s.x.length; i++) {
      if (!this.usable(s.x[i], s.y[i])) continue;
      const d = (X(s.x[i]) - mx) ** 2 + (Y(s.y[i]) - my) ** 2;
      if (d < bestD) { bestD = d; best = { s, i }; }
    }
    this.hover = best;
    this.draw();
  }

  downloadPng(filename) {
    // composite onto an opaque background so the png is readable outside the app
    const out = document.createElement('canvas');
    out.width = this.canvas.width; out.height = this.canvas.height;
    const g = out.getContext('2d');
    g.fillStyle = css('--surface') || '#fff';
    g.fillRect(0, 0, out.width, out.height);
    g.drawImage(this.canvas, 0, 0);
    const a = document.createElement('a');
    a.href = out.toDataURL('image/png');
    a.download = filename;
    a.click();
  }
}
