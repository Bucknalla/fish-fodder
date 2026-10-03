// Weather for the header: turning forecast codes into a few kinds of sky, and
// drawing each one as a small pen-stroke icon to match the fish.

// Open-Meteo reports WMO weather codes.
// https://open-meteo.com/en/docs#weather_variable_documentation
export function weatherKind(code) {
  if (code === 0) return 'sun';
  if (code === 1 || code === 2) return 'partly';
  if (code === 3) return 'cloud';
  if (code === 45 || code === 48) return 'fog';
  if (code >= 51 && code <= 57) return 'drizzle';
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return 'rain';
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 'snow';
  if (code >= 95 && code <= 99) return 'storm';
  return 'cloud';
}

export const KIND_LABELS = {
  sun: 'Sunny', partly: 'Partly cloudy', cloud: 'Cloudy', fog: 'Fog',
  drizzle: 'Drizzle', rain: 'Rain', snow: 'Snow', storm: 'Thunderstorms',
};

// Plausible days for the simulator's sample weather (°C high/low).
export const SAMPLES = {
  sun: [22, 13], partly: [19, 11], cloud: [16, 10], fog: [11, 6],
  drizzle: [14, 9], rain: [13, 8], snow: [1, -4], storm: [24, 17],
};

export const toFahrenheit = (c) => (c * 9) / 5 + 32;

// ---------------------------------------------------------------------------
// Icons. Each is drawn in a unit box (0..1 across, 0..1 down) and then scaled.

const TAU = Math.PI * 2;

function arc(cx, cy, r, a0, a1, n = 24) {
  const pts = [];
  for (let i = 0; i <= n; i++) {
    const a = a0 + ((a1 - a0) * i) / n;
    pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]);
  }
  return pts;
}

const PUFFS = [[0.3, 0.58, 0.17], [0.53, 0.44, 0.23], [0.77, 0.58, 0.16]];
const BASE = 0.74;
const halfChord = ([, cy, r]) => Math.sqrt(Math.max(0, r * r - (BASE - cy) ** 2));
const LEFT = PUFFS[0][0] - halfChord(PUFFS[0]);
const RIGHT = PUFFS[2][0] + halfChord(PUFFS[2]);

// Top edge of the cloud at x (unit cloud coordinates), or Infinity outside it.
function cloudTop(x) {
  let top = Infinity;
  for (const [cx, cy, r] of PUFFS) {
    if (Math.abs(x - cx) <= r) top = Math.min(top, cy - Math.sqrt(Math.max(0, r * r - (x - cx) ** 2)));
  }
  return x >= LEFT && x <= RIGHT ? top : Infinity;
}

// Is (x, y) inside the cloud silhouette (unit cloud coordinates)? `margin`
// grows the cloud slightly.
function insideCloud(x, y, margin = 0) {
  if (y > BASE + margin) return false;
  if (PUFFS.some(([cx, cy, r]) => (x - cx) ** 2 + (y - cy) ** 2 < (r + margin) ** 2)) return true;
  return x > LEFT - margin && x < RIGHT + margin && y > cloudTop(x) - margin;
}

// A cloud silhouette: the visible arcs of three overlapping puffs over a flat
// base, offset by (dx, dy) and scaled by k.
function cloud(dx = 0, dy = 0, k = 1) {
  const out = [];
  PUFFS.forEach(([cx, cy, r], i) => {
    const others = PUFFS.filter((_, j) => j !== i);
    let run = [];
    for (let s = 0; s <= 96; s++) {
      const a = (s / 96) * TAU;
      const x = cx + r * Math.cos(a);
      const y = cy + r * Math.sin(a);
      const hidden = y > BASE + 1e-6
        || others.some(([ox, oy, or]) => (x - ox) ** 2 + (y - oy) ** 2 < (or - 1e-3) ** 2)
        || (x > LEFT && x < RIGHT && y > cloudTop(x) + 0.01);
      if (hidden) {
        if (run.length > 1) out.push(run);
        run = [];
      } else {
        run.push([x, y]);
      }
    }
    if (run.length > 1) out.push(run);
  });
  out.push([[LEFT, BASE], [RIGHT, BASE]]);
  return out.map((pl) => pl.map(([x, y]) => [dx + x * k, dy + y * k]));
}

// Cut away the parts of `polylines` hidden behind a cloud(dx, dy, k).
function behindCloud(polylines, dx, dy, k) {
  const hidden = ([x, y]) => insideCloud((x - dx) / k, (y - dy) / k, 0.04);
  const out = [];
  for (const pl of polylines) {
    let run = [];
    for (let i = 0; i < pl.length; i++) {
      // Resample each segment so the cut lands close to the outline.
      const [x0, y0] = pl[i];
      const [x1, y1] = pl[i + 1] ?? pl[i];
      const steps = i + 1 < pl.length ? 12 : 1;
      for (let j = 0; j < steps; j++) {
        const p = [x0 + ((x1 - x0) * j) / steps, y0 + ((y1 - y0) * j) / steps];
        if (hidden(p)) {
          if (run.length > 1) out.push(run);
          run = [];
        } else {
          run.push(p);
        }
      }
    }
    if (run.length > 1) out.push(run);
  }
  return out;
}

function sun(cx, cy, r) {
  const out = [arc(cx, cy, r, 0, TAU, 32)];
  for (let i = 0; i < 8; i++) {
    const a = (i / 8) * TAU;
    out.push([[cx + r * 1.45 * Math.cos(a), cy + r * 1.45 * Math.sin(a)], [cx + r * 1.9 * Math.cos(a), cy + r * 1.9 * Math.sin(a)]]);
  }
  return out;
}

function slashes(n, y0, y1, slant, xs) {
  return xs.slice(0, n).map((x) => [[x + slant, y0], [x, y1]]);
}

const ICONS = {
  sun: () => sun(0.5, 0.5, 0.2),
  // A sun peeking out from behind the top-left of a cloud.
  partly: () => [...behindCloud(sun(0.36, 0.34, 0.15), 0.1, 0.14, 0.9), ...cloud(0.1, 0.14, 0.9)],
  cloud: () => cloud(0, 0.05),
  fog: () => [[[0.12, 0.38], [0.88, 0.38]], [[0.2, 0.54], [0.8, 0.54]], [[0.12, 0.7], [0.88, 0.7]], [[0.25, 0.86], [0.75, 0.86]]],
  drizzle: () => [...cloud(0, -0.1), ...slashes(3, 0.76, 0.86, 0.03, [0.32, 0.5, 0.68])],
  rain: () => [...cloud(0, -0.1), ...slashes(3, 0.74, 0.98, 0.07, [0.3, 0.5, 0.7])],
  snow: () => [
    ...cloud(0, -0.1),
    ...[0.32, 0.5, 0.68].flatMap((x, i) => {
      const y = i === 1 ? 0.9 : 0.82;
      const s = 0.06;
      return [0, 1, 2].map((j) => {
        const a = (j / 3) * Math.PI + Math.PI / 2;
        return [[x - s * Math.cos(a), y - s * Math.sin(a)], [x + s * Math.cos(a), y + s * Math.sin(a)]];
      });
    }),
  ],
  storm: () => [...cloud(0, -0.1), [[0.56, 0.66], [0.44, 0.84], [0.56, 0.84], [0.46, 1.0]]],
};

// Polylines for `kind`'s icon in a square of side `size` with its top-left
// corner at (x, y).
export function weatherIcon(kind, { x, y, size }) {
  return (ICONS[kind] ?? ICONS.cloud)().map((pl) => pl.map(([u, v]) => [x + u * size, y + v * size]));
}
