// A 1-bit bitmap and a tiny stroke rasteriser. E-ink panels are (mostly)
// black and white, so we draw straight to 1 bit per pixel instead of
// anti-aliasing and dithering later. 1 = ink, 0 = paper.

export class Bitmap {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.data = new Uint8Array(width * height);
  }

  get(x, y) {
    return this.data[y * this.width + x];
  }

  fillRect(x0, y0, w, h, v = 1) {
    const xa = Math.max(0, Math.round(x0));
    const ya = Math.max(0, Math.round(y0));
    const xb = Math.min(this.width, Math.round(x0 + w));
    const yb = Math.min(this.height, Math.round(y0 + h));
    for (let y = ya; y < yb; y++) this.data.fill(v, y * this.width + xa, y * this.width + xb);
  }

  // Fill every pixel whose centre is within `r` of segment (x0,y0)-(x1,y1).
  // With r >= 0.5 a line never has gaps, whatever its angle.
  segment(x0, y0, x1, y1, r) {
    const { width: W, height: H, data } = this;
    const xa = Math.max(0, Math.floor(Math.min(x0, x1) - r));
    const xb = Math.min(W - 1, Math.ceil(Math.max(x0, x1) + r));
    const ya = Math.max(0, Math.floor(Math.min(y0, y1) - r));
    const yb = Math.min(H - 1, Math.ceil(Math.max(y0, y1) + r));
    const dx = x1 - x0;
    const dy = y1 - y0;
    const len2 = dx * dx + dy * dy;
    const r2 = r * r;
    for (let y = ya; y <= yb; y++) {
      const py = y + 0.5 - y0;
      for (let x = xa; x <= xb; x++) {
        const px = x + 0.5 - x0;
        let t = len2 ? (px * dx + py * dy) / len2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const ex = px - t * dx;
        const ey = py - t * dy;
        if (ex * ex + ey * ey <= r2) data[y * W + x] = 1;
      }
    }
  }

  stroke(polylines, width = 1) {
    const r = Math.max(0.5, width / 2);
    for (const pl of polylines) {
      if (pl.length === 1) this.segment(pl[0][0], pl[0][1], pl[0][0], pl[0][1], r);
      for (let i = 1; i < pl.length; i++) {
        this.segment(pl[i - 1][0], pl[i - 1][1], pl[i][0], pl[i][1], r);
      }
    }
  }

  // Rotate clockwise by 0/90/180/270 degrees, returning a new bitmap.
  rotate(deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg === 0) return this;
    const { width: W, height: H } = this;
    const out = deg === 180 ? new Bitmap(W, H) : new Bitmap(H, W);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        if (!this.data[y * W + x]) continue;
        let nx, ny;
        if (deg === 90) [nx, ny] = [H - 1 - y, x];
        else if (deg === 180) [nx, ny] = [W - 1 - x, H - 1 - y];
        else [nx, ny] = [y, W - 1 - x];
        out.data[ny * out.width + nx] = 1;
      }
    }
    return out;
  }

  // Pack to 1 bit per pixel, MSB first, rows padded to whole bytes.
  // Bits are set for *paper* (white), which is what both PNG grayscale and
  // most e-paper controllers expect.
  pack() {
    const { width: W, height: H, data } = this;
    const stride = Math.ceil(W / 8);
    const out = new Uint8Array(stride * H);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        if (!data[y * W + x]) out[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
    return { stride, bytes: out };
  }
}
