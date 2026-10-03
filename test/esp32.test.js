// The ESP32 app's forecast reading (esp32/main/forecast.c), built and run on
// the host. Skipped when there's no C compiler.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const out = `${root}c/build/forecast_test`;
mkdirSync(`${root}c/build`, { recursive: true });
const built = spawnSync('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-o', out,
  `${root}esp32/test/forecast_test.c`, `${root}esp32/main/forecast.c`], { encoding: 'utf8' });
const skip = built.error ? `no C compiler: ${built.error.message}` : false;

test('the ESP32 app reads Open-Meteo and ipinfo.io replies', { skip }, () => {
  assert.equal(built.status, 0, built.stderr);
  const run = spawnSync(out, { encoding: 'utf8' });
  assert.equal(run.status, 0, run.stderr);
  assert.equal(run.stdout.trim(), 'ok');
});

test("the ESP32 self-check expects the JavaScript's fish", async () => {
  const { draw_fish } = await import('../src/vendor/fishdraw.js');
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(`${root}esp32/main/main.c`, 'utf8');
  const name = src.match(/static const char \*NAME = "([^"]*)"/)[1];
  const expected = src.match(/EXPECTED = 0x([0-9a-f]{16})ull/)[1];
  // FNV-1a over each line's length (int32) and points (two float64s each),
  // little-endian, as self_check() hashes them.
  let h = 0xcbf29ce484222325n;
  const mix = (bytes) => { for (const b of bytes) h = BigInt.asUintN(64, (h ^ BigInt(b)) * 0x100000001b3n); };
  const v = new DataView(new ArrayBuffer(8));
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
  assert.equal(h.toString(16).padStart(16, '0'), expected);
});
