// The JavaScript side of the Xtensa check (run.sh):
//   node compare.mjs names DIR COUNT   writes DIR/names.h with COUNT+3 names
//   node compare.mjs check DIR         compares DIR/out.txt with the JS
import { readFileSync, writeFileSync } from 'node:fs';
import { draw_fish } from '../../src/vendor/fishdraw.js';
import { generateCatch } from '../../src/names.js';
import { renderFrame } from '../../src/render.js';

const [cmd, dir, count = '80'] = process.argv.slice(2);

if (cmd === 'names') {
  const names = ['Biggus fishus', '', 'Ünïcödé Fïsh 🐟', ...Array.from({ length: +count }, (_, i) => generateCatch(`xtensa ${i}`).name)];
  const esc = (s) => `"${[...Buffer.from(s)].map((b) => (b < 0x20 || b > 0x7e || b === 34 || b === 92 ? `\\${b.toString(8).padStart(3, '0')}` : String.fromCharCode(b))).join('')}"`;
  writeFileSync(`${dir}/names.h`, `static const char *const NAMES[] = {\n${names.map((n) => `  ${esc(n)},`).join('\n')}\n};\n`);
  writeFileSync(`${dir}/names.json`, JSON.stringify(names));
  process.exit(0);
}

// FNV-1a, 64-bit, as check.c computes it.
let h;
const mix = (bytes) => { for (const b of bytes) h = BigInt.asUintN(64, (h ^ BigInt(b)) * 0x100000001b3n); };
const hex = () => h.toString(16).padStart(16, '0');
const v = new DataView(new ArrayBuffer(8));

const expected = [];
JSON.parse(readFileSync(`${dir}/names.json`, 'utf8')).forEach((name, i) => {
  h = 0xcbf29ce484222325n;
  for (const pl of draw_fish(name)) {
    v.setInt32(0, pl.length, true);
    mix(new Uint8Array(v.buffer, 0, 4));
    for (const [x, y] of pl) {
      for (const n of [x, y]) {
        v.setFloat64(0, n, true);
        mix(new Uint8Array(v.buffer, 0, 8));
      }
    }
  }
  expected.push(`fish ${i} ${hex()}`);
});
// The same frames as check.c's table.
[
  [[2026, 10, 3, 14, 5], 800, 480, 0, '24h', 'minute', ['partly', 19, 11]],
  [[2026, 1, 31, 0, 59], 800, 480, 90, '12h', 'hour', ['rain', -0.4, -12.6]],
  [[2026, 7, 4, 12, 30], 400, 300, 0, '12h', 'minute', ['snow', 1, -4]],
  [[2026, 3, 9, 23, 1], 296, 128, 270, '24h', 'minute', ['storm', 24, 17]],
].forEach(([[y, mo, d, hh, mi], width, height, rotate, clock, precision, [kind, high, low]], i) => {
  const { bitmap } = renderFrame(new Date(y, mo - 1, d, hh, mi), { width, height, rotate, clock, precision, weather: { kind, high, low } });
  h = 0xcbf29ce484222325n;
  mix(bitmap.data);
  expected.push(`frame ${i} ${hex()}`);
});

const got = readFileSync(`${dir}/out.txt`, 'utf8').trim().split('\n').map((l) => l.split(' ').slice(0, 3).join(' '));
const same = expected.filter((e, i) => e === got[i]).length;
console.log(`${same}/${expected.length} identical on Xtensa`);
expected.forEach((e, i) => { if (e !== got[i]) console.log(`  expected ${e}, got ${got[i] ?? 'nothing'}`); });
process.exit(same === expected.length ? 0 : 1);
