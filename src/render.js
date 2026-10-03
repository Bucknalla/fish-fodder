// Compose a clock face: the time, today's date, this hour's fish, its name
// and a field note, rendered to a 1-bit bitmap at the panel's resolution.

import { draw_fish } from './vendor/fishdraw.js';
import { Bitmap } from './raster.js';
import { measureText, textPolylines } from './text.js';
import { catchFor } from './schedule.js';

// Common panels. Any width/height works; these are just handy.
export const PRESETS = {
  'waveshare-7in5': { label: 'Waveshare 7.5" (800×480)', width: 800, height: 480 },
  'waveshare-7in5-hd': { label: 'Waveshare 7.5" HD (880×528)', width: 880, height: 528 },
  'inky-impression-7in3': { label: 'Inky Impression 7.3" (800×480)', width: 800, height: 480 },
  'waveshare-5in83': { label: 'Waveshare 5.83" (648×480)', width: 648, height: 480 },
  'inky-impression-5in7': { label: 'Inky Impression 5.7" (600×448)', width: 600, height: 448 },
  'inky-what': { label: 'Inky wHAT 4.2" (400×300)', width: 400, height: 300 },
  'waveshare-4in2': { label: 'Waveshare 4.2" (400×300)', width: 400, height: 300 },
  'waveshare-2in9': { label: 'Waveshare 2.9" (296×128)', width: 296, height: 128 },
};

export const DEFAULTS = {
  width: 800,
  height: 480,
  rotate: 0, // degrees clockwise, for frames mounted portrait or upside down
  clock: '24h', // or '12h'
  precision: 'minute', // or 'hour' for panels that only refresh hourly
  salt: '',
};

const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
  'August', 'September', 'October', 'November', 'December'];

export function formatTime(date, { clock = '24h', precision = 'minute' } = {}) {
  const mm = precision === 'hour' ? '00' : String(date.getMinutes()).padStart(2, '0');
  const h = date.getHours();
  if (clock === '12h') return { time: `${h % 12 || 12}:${mm}`, suffix: h < 12 ? 'am' : 'pm' };
  return { time: `${String(h).padStart(2, '0')}:${mm}`, suffix: '' };
}

function formatDates(date) {
  const d = date.getDate();
  return [
    `${DAYS[date.getDay()]} ${d} ${MONTHS[date.getMonth()]}`,
    `${DAYS[date.getDay()].slice(0, 3)} ${d} ${MONTHS[date.getMonth()].slice(0, 3)}`,
  ];
}

// fishdraw takes ~0.5s per fish, and the fish only changes hourly, so keep
// the last few around for the per-minute clock redraws.
const fishCache = new Map();
export function fishFor(name) {
  if (fishCache.has(name)) return fishCache.get(name);
  const polylines = draw_fish(name);
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const pl of polylines) {
    for (const [x, y] of pl) {
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  const fish = { polylines, bbox: { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } };
  fishCache.set(name, fish);
  if (fishCache.size > 4) fishCache.delete(fishCache.keys().next().value);
  return fish;
}

// Lay out and draw one frame. Returns { bitmap, catch }.
export function renderFrame(date, options = {}) {
  const opts = { ...DEFAULTS, ...options };
  const sideways = opts.rotate % 180 !== 0;
  const W = sideways ? opts.height : opts.width;
  const H = sideways ? opts.width : opts.height;
  const bmp = new Bitmap(W, H);
  const fishCatch = options.catch ?? catchFor(date, opts);

  // Everything scales off one reference length so small and portrait panels
  // keep the same proportions.
  const u = Math.min(H, W * 0.62);
  const m = Math.max(4, Math.round(Math.min(W, H) * 0.045));
  const tiny = H < 200;
  const inner = W - 2 * m;

  // --- header: big time on the left, date (and rare badge) on the right
  const { time, suffix } = formatTime(date, opts);
  const timeSize = u * (tiny ? 0.2 : 0.13);
  const timeY = m + timeSize;
  bmp.stroke(textPolylines(time, { x: m, y: timeY, font: 'duplex', size: timeSize }), Math.max(1.5, timeSize / 14));
  let headerRight = m + measureText(time, { font: 'duplex', size: timeSize });
  if (suffix) {
    const s = timeSize * 0.4;
    bmp.stroke(textPolylines(suffix, { x: headerRight + s * 0.3, y: timeY, size: s }), Math.max(1, s / 10));
    headerRight += s * 0.3 + measureText(suffix, { size: s });
  }

  const dateSize = Math.max(7, u * 0.042);
  const room = W - m - headerRight - m;
  const dateText = formatDates(date).find((t) => measureText(t, { size: dateSize }) <= room);
  if (dateText) {
    const w = measureText(dateText, { size: dateSize });
    bmp.stroke(textPolylines(dateText, { x: W - m - w, y: timeY, size: dateSize }), Math.max(1, dateSize / 12));
  }
  if (fishCatch.rare && !tiny) {
    const s = dateSize * 0.8;
    const label = 'RARE CATCH!';
    const w = measureText(label, { size: s, tracking: 3 });
    const pad = s * 0.45;
    const x = W - m - w - pad;
    const y = m + s + pad;
    bmp.stroke(textPolylines(label, { x, y, size: s, tracking: 3 }), Math.max(1, s / 10));
    bmp.stroke([[[x - pad, m], [x + w + pad, m], [x + w + pad, y + pad], [x - pad, y + pad], [x - pad, m]]], Math.max(1, s / 10));
  }

  const ruleY = timeY + m * 0.7;
  bmp.stroke([[[m, ruleY], [W - m, ruleY]]], Math.max(1, u / 300));

  // --- footer: italic name (like fishdraw's own labels) and a field note.
  // Descenders reach DESCENT × size below the baseline.
  const DESCENT = 0.36;
  let bottom = H - m;
  let nameBaseline = null;
  let noteSize = Math.max(7, u * 0.036);
  noteSize = Math.min(noteSize, noteSize * inner / measureText(fishCatch.note, { size: noteSize }));
  if (!tiny && noteSize >= 7) {
    const w = measureText(fishCatch.note, { size: noteSize });
    const y = bottom - noteSize * DESCENT;
    bmp.stroke(textPolylines(fishCatch.note, { x: (W - w) / 2, y, size: noteSize }), Math.max(1, noteSize / 12));
    nameBaseline = y - noteSize * 1.9;
  }

  const italic = 0.3;
  let nameSize = u * (tiny ? 0.09 : 0.062);
  // Italic letters lean right by ~italic*cap height; leave room for it.
  nameSize = Math.min(nameSize, inner / (measureText(fishCatch.name, { size: 1 }) + italic));
  const nameW = measureText(fishCatch.name, { size: nameSize });
  bottom = nameBaseline ?? bottom - nameSize * DESCENT;
  bmp.stroke(
    textPolylines(fishCatch.name, { x: (W - nameW) / 2 - (italic * nameSize) / 2, y: bottom, size: nameSize, italic }),
    Math.max(1, nameSize / 11),
  );

  // --- the fish, fitted into whatever's left
  const boxTop = ruleY + m * 0.6;
  const boxBottom = bottom - nameSize * 1.05 - m * 0.6;
  const { polylines, bbox } = fishFor(fishCatch.name);
  const scale = Math.min(inner / bbox.w, (boxBottom - boxTop) / bbox.h);
  const ox = m + (inner - bbox.w * scale) / 2 - bbox.x * scale;
  const oy = boxTop + (boxBottom - boxTop - bbox.h * scale) / 2 - bbox.y * scale;
  bmp.stroke(
    polylines.map((pl) => pl.map(([x, y]) => [x * scale + ox, y * scale + oy])),
    Math.max(1, scale * 0.9),
  );

  return { bitmap: opts.rotate ? bmp.rotate(opts.rotate) : bmp, catch: fishCatch };
}
