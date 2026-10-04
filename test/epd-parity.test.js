// The C e-paper drivers (c/epd) must send exactly what the vendors' drivers
// send. Both run against fake hardware (test/epd-vendor/trace.py and
// c/epd/epd_trace) and their SPI bytes, pin changes and delays are compared.
// Skipped without a C compiler, or Python with Pillow and numpy.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const built = spawnSync('make', ['-C', `${root}c`, '-s'], { encoding: 'utf8' });
const python = spawnSync('python3', ['-c', 'import PIL, numpy'], { encoding: 'utf8' });
const skip = built.status !== 0 ? 'C build unavailable'
  : python.status !== 0 ? 'needs python3 with Pillow and numpy' : false;

const PANELS = ['waveshare-7in5-v2', 'inky-ac073tc1a', 'inky-e673'];

test('the C e-paper drivers send what the vendors\' drivers send', { skip }, (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'epd-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  // A full refresh, then two minutes' partial refreshes (where supported).
  const frames = ['14:05', '14:06', '14:07'].map((hm, i) => {
    const path = join(dir, `frame${i}.pbm`);
    execFileSync(`${root}c/build/frame_cli`, ['--time', `2026-10-04T${hm}`, '--weather', 'rain,14,8', '-o', path], { stdio: 'ignore' });
    return path;
  });
  for (const panel of PANELS) {
    const c = execFileSync(`${root}c/build/epd_trace`, [panel, ...frames], { encoding: 'utf8' });
    const vendor = execFileSync('python3', [`${root}test/epd-vendor/trace.py`, panel, ...frames], { encoding: 'utf8' });
    assert.ok(c.split('\n').length > 100, `${panel}: trace looks too short`);
    assert.equal(c, vendor, panel);
  }
});
