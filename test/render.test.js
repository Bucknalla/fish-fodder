import { test } from 'node:test';
import assert from 'node:assert/strict';
import { inflateSync } from 'node:zlib';
import { draw_fish } from '../src/vendor/fishdraw.js';
import { Bitmap } from '../src/raster.js';
import { renderFrame, formatTime, PRESETS } from '../src/render.js';
import { measureText, textPolylines } from '../src/text.js';
import { encodePNG } from '../src/node/png.js';

const AT = new Date(2026, 9, 3, 14, 37);
const inked = (bmp) => bmp.data.reduce((s, v) => s + v, 0) / bmp.data.length;

test('a name draws the same fish whatever was drawn before it', () => {
  const first = JSON.stringify(draw_fish('Biggus fishus'));
  draw_fish('Lord Kipperbottom');
  assert.equal(JSON.stringify(draw_fish('Biggus fishus')), first);
});

test('formatTime', () => {
  assert.deepEqual(formatTime(AT), { time: '14:37', suffix: '' });
  assert.deepEqual(formatTime(AT, { clock: '12h' }), { time: '2:37', suffix: 'pm' });
  assert.deepEqual(formatTime(new Date(2026, 0, 1, 0, 5), { clock: '12h' }), { time: '12:05', suffix: 'am' });
  assert.deepEqual(formatTime(AT, { precision: 'hour' }), { time: '14:00', suffix: '' });
});

test('text measures what it draws', () => {
  const size = 40;
  const w = measureText('Hello, fish!', { size });
  const xs = textPolylines('Hello, fish!', { size }).flat().map(([x]) => x);
  assert.ok(Math.max(...xs) <= w + 0.01);
  assert.ok(Math.max(...xs) > w * 0.9);
});

test('strokes have no gaps at any angle', () => {
  for (let a = 0; a < Math.PI; a += 0.1) {
    const b = new Bitmap(60, 60);
    b.stroke([[[30 - 25 * Math.cos(a), 30 - 25 * Math.sin(a)], [30 + 25 * Math.cos(a), 30 + 25 * Math.sin(a)]]], 1);
    // every row or every column the line spans has ink
    const horizontal = Math.abs(Math.cos(a)) > Math.abs(Math.sin(a));
    const lo = Math.ceil(30 - 24 * Math.abs(horizontal ? Math.cos(a) : Math.sin(a)));
    for (let i = lo; i < 60 - lo; i++) {
      let any = 0;
      for (let j = 0; j < 60; j++) any |= horizontal ? b.get(i, j) : b.get(j, i);
      assert.equal(any, 1, `gap at angle ${a.toFixed(1)}, ${i}`);
    }
  }
});

test('rotate', () => {
  const b = new Bitmap(4, 2);
  b.data[1] = 1; // (1,0)
  const r = b.rotate(90);
  assert.equal(r.width, 2);
  assert.equal(r.height, 4);
  assert.equal(r.get(1, 1), 1);
  assert.equal(b.rotate(180).get(2, 1), 1);
  assert.equal(b.rotate(270).get(0, 2), 1);
});

test('every preset renders a sensible frame', () => {
  for (const [name, p] of Object.entries(PRESETS)) {
    const { bitmap } = renderFrame(AT, p);
    assert.equal(bitmap.width, p.width, name);
    assert.equal(bitmap.height, p.height, name);
    const ink = inked(bitmap);
    assert.ok(ink > 0.02 && ink < 0.4, `${name}: ${(ink * 100).toFixed(1)}% ink`);
  }
});

test('nothing is drawn in the margins', () => {
  for (const clock of ['24h', '12h']) {
    for (let h = 0; h < 24; h += 5) {
      const { bitmap } = renderFrame(new Date(2026, 9, 3, h, 58), { clock });
      const m = 12;
      for (let y = 0; y < bitmap.height; y++) {
        for (let x = 0; x < bitmap.width; x++) {
          if (x < m || y < m || x >= bitmap.width - m || y >= bitmap.height - m) {
            assert.equal(bitmap.get(x, y), 0, `ink at ${x},${y} (hour ${h}, ${clock})`);
          }
        }
      }
    }
  }
});

test('rotated frames swap the panel dimensions', () => {
  const { bitmap } = renderFrame(AT, { width: 800, height: 480, rotate: 90 });
  assert.equal(bitmap.width, 800);
  assert.equal(bitmap.height, 480);
});

test('PNG output decodes back to the bitmap', () => {
  const { bitmap } = renderFrame(AT, PRESETS['waveshare-2in9']);
  const png = encodePNG(bitmap);
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  assert.equal(png.readUInt32BE(16), bitmap.width);
  assert.equal(png.readUInt32BE(20), bitmap.height);
  const idatLen = png.readUInt32BE(33);
  const raw = inflateSync(png.subarray(41, 41 + idatLen));
  const stride = Math.ceil(bitmap.width / 8);
  for (let y = 0; y < bitmap.height; y++) {
    for (let x = 0; x < bitmap.width; x++) {
      const white = (raw[y * (stride + 1) + 1 + (x >> 3)] >> (7 - (x & 7))) & 1;
      assert.equal(white, 1 - bitmap.get(x, y));
    }
  }
});
