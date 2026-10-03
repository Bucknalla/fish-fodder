// Turn strings into polylines using the Hershey stroke fonts.

import { ROMAN_SIMPLEX, ROMAN_DUPLEX } from './hershey-data.js';

export const FONTS = { simplex: ROMAN_SIMPLEX, duplex: ROMAN_DUPLEX };

// Hershey units: capitals run from y=-12 to the baseline at y=9.
const CAP_HEIGHT = 21;
const BASELINE = 9;
const ORD_R = 'R'.charCodeAt(0);

const cache = new Map();

function glyph(font, ch) {
  const key = `${font === ROMAN_DUPLEX ? 'd' : 's'}${ch}`;
  if (cache.has(key)) return cache.get(key);
  const code = ch.charCodeAt(0);
  const entry = font[code - 32] ?? font['?'.charCodeAt(0) - 32];
  const left = entry.charCodeAt(3) - ORD_R;
  const right = entry.charCodeAt(4) - ORD_R;
  const strokes = [[]];
  for (let i = 5; i < entry.length; i += 2) {
    if (entry[i] === ' ' && entry[i + 1] === 'R') {
      strokes.push([]);
    } else {
      strokes[strokes.length - 1].push([
        entry.charCodeAt(i) - ORD_R - left,
        entry.charCodeAt(i + 1) - ORD_R - BASELINE,
      ]);
    }
  }
  const g = { advance: right - left, strokes: strokes.filter((s) => s.length > 1) };
  cache.set(key, g);
  return g;
}

// Width in pixels of `str` drawn with capitals `size` px tall.
export function measureText(str, { font = 'simplex', size, tracking = 0 }) {
  const f = FONTS[font];
  let w = 0;
  for (const ch of str) w += glyph(f, ch).advance + tracking;
  return (w - (str.length ? tracking : 0)) * (size / CAP_HEIGHT);
}

// Polylines for `str` with its baseline-left corner at (x, y).
// `italic` shears like fishdraw's own labels (it uses 0.3).
export function textPolylines(str, { x = 0, y = 0, font = 'simplex', size, italic = 0, tracking = 0 }) {
  const f = FONTS[font];
  const s = size / CAP_HEIGHT;
  const out = [];
  let pen = 0;
  for (const ch of str) {
    const g = glyph(f, ch);
    for (const stroke of g.strokes) {
      out.push(stroke.map(([gx, gy]) => [x + (pen + gx - italic * gy) * s, y + gy * s]));
    }
    pen += g.advance + tracking;
  }
  return out;
}
