// The C port (c/) must draw exactly the same fish and frames as the JavaScript.
// Skipped when there's no C compiler.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { draw_fish } from '../src/vendor/fishdraw.js';
import { generateCatch } from '../src/names.js';
import { renderFrame } from '../src/render.js';
import { catchFor } from '../src/schedule.js';

const cdir = fileURLToPath(new URL('../c', import.meta.url));
const built = spawnSync('make', ['-C', cdir, '-s'], { encoding: 'utf8' });
const skip = built.status !== 0 && `C build unavailable: ${(built.stderr || built.error?.message || '').trim().split('\n')[0]}`;

test('fishdraw in C draws the same fish as fishdraw in JS', { skip }, () => {
  const names = ['Biggus fishus', '', 'Ünïcödé Fïsh 🐟', ...Array.from({ length: 12 }, (_, i) => generateCatch(`c-parity ${i}`).name)];
  const out = execFileSync(`${cdir}/build/fishdraw_cli`, names, { maxBuffer: 1 << 28 }).toString().trim().split('\n');
  names.forEach((name, i) => {
    // %.17g in C parses back to the exact double; stringify both the JS way.
    assert.equal(JSON.stringify(JSON.parse(out[i])), JSON.stringify(draw_fish(name)), name);
  });
});

// The whole frame: header, weather icons, lettering and fish, pixel for pixel.
const frames = [
  { at: [2026, 10, 3, 14, 5], weather: ['partly', 19, 11] },
  { at: [2026, 1, 31, 0, 59], width: 800, height: 480, rotate: 90, clock: '12h', precision: 'hour', weather: ['rain', -0.4, -12.6] },
  { at: [2026, 7, 4, 12, 30], width: 400, height: 300, salt: 'kitchen', clock: '12h', weather: ['snow', 1, -4] },
  { at: [2026, 3, 9, 23, 1], width: 296, height: 128, rotate: 270, weather: ['storm', 24, 17] },
  { at: [2026, 5, 17, 8, 45], width: 648, height: 480, rotate: 180, weather: ['fog', 11, 6] },
  { at: [2026, 12, 25, 18, 0], width: 880, height: 528, weather: ['drizzle', 104.5, 88] },
  { at: [2026, 2, 14, 9, 9], width: 600, height: 448, weather: ['sun', 22, 13] },
  { at: [2026, 9, 1, 6, 0], weather: ['cloud', 16, 10], rotate: 360 },
  { at: [2026, 6, 21, 21, 21], catch: { name: 'Sir Reginald Bubblesworth III', note: 'Habitat: the deep end of the hotel pool, after hours', rare: true } },
];

function cFrame(f) {
  const [y, mo, d, h, mi] = f.at;
  const p = (n) => String(n).padStart(2, '0');
  const args = ['--time', `${y}-${p(mo)}-${p(d)}T${p(h)}:${p(mi)}`, '--size', `${f.width ?? 800}x${f.height ?? 480}`];
  if (f.rotate) args.push('--rotate', String(f.rotate));
  if (f.clock === '12h') args.push('--12h');
  if (f.precision === 'hour') args.push('--hourly');
  if (f.salt) args.push('--salt', f.salt);
  if (f.weather) args.push('--weather', f.weather.join(','));
  if (f.catch) args.push('--name', f.catch.name, '--note', f.catch.note, ...(f.catch.rare ? ['--rare'] : []));
  const pbm = execFileSync(`${cdir}/build/frame_cli`, args, { stdio: ['ignore', 'pipe', 'ignore'], maxBuffer: 1 << 24 });
  const [, w, h2] = pbm.toString('latin1', 0, 32).match(/^P4\n(\d+) (\d+)\n/);
  const header = `P4\n${w} ${h2}\n`.length;
  const width = +w, height = +h2, stride = Math.ceil(width / 8);
  const data = new Uint8Array(width * height);
  for (let yy = 0; yy < height; yy++) {
    for (let x = 0; x < width; x++) data[yy * width + x] = (pbm[header + yy * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
  }
  return { width, height, data };
}

test('the C frame renderer draws the same pixels as renderFrame', { skip }, () => {
  for (const f of frames) {
    const [y, mo, d, h, mi] = f.at;
    const [kind, high, low] = f.weather ?? [];
    const { bitmap } = renderFrame(new Date(y, mo - 1, d, h, mi), {
      width: f.width ?? 800, height: f.height ?? 480, rotate: f.rotate ?? 0, clock: f.clock ?? '24h',
      precision: f.precision ?? 'minute', salt: f.salt ?? '', weather: f.weather ? { kind, high, low } : null,
      ...(f.catch ? { catch: f.catch } : {}),
    });
    const c = cFrame(f);
    const label = JSON.stringify(f);
    assert.deepEqual([c.width, c.height], [bitmap.width, bitmap.height], label);
    let diff = 0;
    for (let i = 0; i < c.data.length; i++) diff += c.data[i] !== bitmap.data[i];
    assert.equal(diff, 0, `${diff} pixels differ: ${label}`);
  }
});

test('the C name generator picks the same catches', { skip }, () => {
  for (let i = 0; i < 120; i++) {
    const at = new Date(2026, 0, 1, i * 7);
    const salt = i % 3 ? '' : `frame ${i}`;
    const p = (n) => String(n).padStart(2, '0');
    const args = ['--catch', '--time', `${at.getFullYear()}-${p(at.getMonth() + 1)}-${p(at.getDate())}T${p(at.getHours())}:00`];
    if (salt) args.push('--salt', salt);
    const c = JSON.parse(execFileSync(`${cdir}/build/frame_cli`, args).toString());
    const { key, name, note, rare } = catchFor(at, { salt });
    assert.deepEqual(c, { key, name, note, rare });
  }
});
