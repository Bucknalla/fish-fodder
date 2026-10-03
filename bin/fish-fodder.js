#!/usr/bin/env node
// fish-fodder: an e-ink clock that catches a new, sillily-named fish every hour.

import { parseArgs } from 'node:util';
import { writeFileSync } from 'node:fs';
import { renderFrame, PRESETS, DEFAULTS } from '../src/render.js';
import { catchesFrom } from '../src/schedule.js';
import { generateCatch } from '../src/names.js';
import { encodePNG } from '../src/node/png.js';
import { runClock } from '../src/node/clock.js';
import { startServer } from '../src/node/server.js';

const HELP = `fish-fodder — a new fish with a silly name every hour

Usage:
  fish-fodder render [options]     draw one frame to a PNG
  fish-fodder names [options]      list upcoming catches
  fish-fodder run --driver <d>     run the clock on a display
  fish-fodder serve                simulator + /frame.png on http://localhost:8080

Frame options:
  --width <px>, --height <px>      panel size (default 800×480, or the panel's own)
  --preset <name>                  ${Object.keys(PRESETS).join(', ')}
  --rotate <0|90|180|270>          for frames mounted portrait or upside down
  --clock <24h|12h>                (default 24h)
  --salt <text>                    different fish from another frame on the same schedule

render:
  --at <date-time>                 e.g. 2026-10-03T14:30 (default now)
  --name <text>                    draw this name instead of the scheduled one
  --out <file.png>                 (default frame.png)

names:
  --at <date-time>, --count <n>    (default now, 24)

run:
  --driver <d>    waveshare:<module>  e.g. waveshare:epd7in5_V2  (needs Waveshare's python lib)
                  inky                Pimoroni Inky, auto-detected (needs the inky python lib)
                  mock                no hardware, logs refreshes
                  file                just write --out on every update
                  exec                write --out then run --exec "cmd {path} {mode}"
  --update <auto|minute|hour>      auto = every minute if the panel can partial-refresh,
                                   otherwise hourly (the time then shows HH:00)
  --out <file.png>, --exec <cmd>, --python <path>

serve:
  --port <n> (default 8080), --host <addr> (default 0.0.0.0)
`;

const { values: o, positionals } = parseArgs({
  allowPositionals: true,
  options: {
    width: { type: 'string' }, height: { type: 'string' }, preset: { type: 'string' },
    rotate: { type: 'string' }, clock: { type: 'string' }, salt: { type: 'string' },
    at: { type: 'string' }, name: { type: 'string' }, out: { type: 'string' },
    count: { type: 'string' }, driver: { type: 'string' }, update: { type: 'string', default: 'auto' },
    exec: { type: 'string' }, python: { type: 'string' }, port: { type: 'string' },
    host: { type: 'string' }, help: { type: 'boolean', short: 'h' },
  },
});

function die(msg) {
  console.error(`fish-fodder: ${msg}`);
  process.exit(1);
}

function frameOpts() {
  const opts = {};
  if (o.preset) {
    if (!PRESETS[o.preset]) die(`unknown preset ${o.preset}`);
    Object.assign(opts, { width: PRESETS[o.preset].width, height: PRESETS[o.preset].height });
  }
  if (o.width) opts.width = Number(o.width);
  if (o.height) opts.height = Number(o.height);
  if (o.rotate) opts.rotate = Number(o.rotate);
  if (o.clock) {
    if (!['24h', '12h'].includes(o.clock)) die('--clock must be 24h or 12h');
    opts.clock = o.clock;
  }
  if (o.salt) opts.salt = o.salt;
  return opts;
}

function when() {
  const d = o.at ? new Date(o.at) : new Date();
  if (Number.isNaN(d.getTime())) die(`can't read date ${o.at}`);
  return d;
}

const cmd = positionals[0];
if (o.help || !cmd) {
  console.log(HELP);
  process.exit(cmd || o.help ? 0 : 1);
}

if (cmd === 'render') {
  const opts = frameOpts();
  if (o.name) opts.catch = { ...generateCatch(o.name), name: o.name, rare: false };
  const { bitmap, catch: c } = renderFrame(when(), opts);
  const out = o.out ?? 'frame.png';
  writeFileSync(out, encodePNG(bitmap));
  console.log(`${out}: ${c.name} — ${c.note}${c.rare ? ' (rare catch!)' : ''}`);
} else if (cmd === 'names') {
  for (const c of catchesFrom(when(), Number(o.count ?? 24), frameOpts())) {
    const hh = String(c.at.getHours()).padStart(2, '0');
    console.log(`${c.key.slice(0, 10)} ${hh}:00  ${c.rare ? '★' : ' '} ${c.name.padEnd(34)} ${c.note}`);
  }
} else if (cmd === 'run') {
  if (!o.driver) die('run needs --driver (try --driver mock)');
  if (!['auto', 'minute', 'hour'].includes(o.update)) die('--update must be auto, minute or hour');
  if (o.driver === 'exec' && !o.exec) die('--driver exec needs --exec "command {path} {mode}"');
  // Without an explicit size, the panel's own resolution is used.
  const f = frameOpts();
  const clock = await runClock({
    ...DEFAULTS, ...f, width: f.width, height: f.height,
    driver: o.driver, update: o.update, out: o.out ?? 'frame.png', exec: o.exec, python: o.python,
  }).catch((err) => die(err.message));
  for (const sig of ['SIGINT', 'SIGTERM']) {
    process.on(sig, async () => {
      await clock.stop().catch(() => {});
      process.exit(0);
    });
  }
} else if (cmd === 'serve') {
  const port = Number(o.port ?? 8080);
  await startServer({ port, host: o.host, defaults: frameOpts() });
  console.log(`fish-fodder simulator on http://localhost:${port}  (frame at /frame.png)`);
} else {
  die(`unknown command ${cmd} (try --help)`);
}
