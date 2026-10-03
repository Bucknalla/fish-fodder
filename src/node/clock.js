// The clock loop: redraw every minute (or hour), with a full refresh whenever
// a new fish arrives and partial refreshes in between where the panel can.

import { renderFrame, fishFor } from '../render.js';
import { hourKey, catchFor } from '../schedule.js';
import { openDisplay } from './displays.js';
import { KIND_LABELS } from '../weather.js';

function msUntilNext(unit, now = new Date()) {
  const next = new Date(now);
  if (unit === 'hour') next.setMinutes(60, 0, 0);
  else next.setSeconds(60, 0);
  return next - now;
}

export async function runClock(opts, log = console.log) {
  const display = await openDisplay(opts);
  const size = {
    width: opts.width ?? display.width ?? 800,
    height: opts.height ?? display.height ?? 480,
  };
  const update = opts.update === 'auto' ? (display.partial ? 'minute' : 'hour') : opts.update;
  if (update === 'minute' && !display.partial) {
    log('warning: this display has no partial refresh, so it will fully refresh every minute');
  }
  const { weatherService, ...frameOpts } = opts;
  const renderOpts = { ...frameOpts, ...size, precision: update };
  log(`fish-fodder: ${size.width}×${size.height}, driver ${opts.driver}, updating every ${update}`);

  let lastKey = null;
  let timer = null;
  let stopping = false;

  async function tick() {
    const now = new Date();
    const key = hourKey(now);
    const mode = key === lastKey ? 'partial' : 'full';
    // Fetch the forecast with each new fish; between fish use what we have,
    // retrying every ten minutes if we have nothing.
    const svc = weatherService;
    let weather = null;
    if (svc && mode === 'full') weather = await svc.forecast(now);
    else if (svc) {
      weather = svc.cached(now);
      if (!weather && now.getMinutes() % 10 === 0) svc.forecast(now);
    }
    try {
      const { bitmap, catch: c } = renderFrame(now, { ...renderOpts, weather });
      await display.show(bitmap, mode);
      if (mode === 'full') {
        const wx = weather ? `  · ${KIND_LABELS[weather.kind]} ${Math.round(weather.high)}°/${Math.round(weather.low)}°` : '';
        log(`${key}:00  ${c.name}${c.rare ? '  (rare catch!)' : ''}${wx}`);
      }
      lastKey = key;
    } catch (err) {
      log(`error: ${err.message}`);
      lastKey = null; // try a full refresh next time
    }
    // Draw the next fish ahead of time so the top of the hour isn't late.
    if (update === 'minute' && now.getMinutes() === 59) {
      const soon = new Date(now.getTime() + 60_000);
      fishFor(catchFor(soon, opts).name);
    }
    if (!stopping) timer = setTimeout(tick, msUntilNext(update) + 50);
  }

  async function stop() {
    if (stopping) return;
    stopping = true;
    clearTimeout(timer);
    await display.close();
  }

  await tick();
  return { stop };
}
