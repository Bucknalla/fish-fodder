import { test } from 'node:test';
import assert from 'node:assert/strict';
import { weatherKind, weatherIcon, KIND_LABELS } from '../src/weather.js';
import { createWeather, localDate } from '../src/node/weather.js';
import { renderFrame, PRESETS } from '../src/render.js';
import { measureText } from '../src/text.js';

// A stand-in for fetch that answers like ipinfo.io and Open-Meteo, and
// records what was asked.
function fakeFetch({ fail = false, code = 61 } = {}) {
  const calls = [];
  const fn = async (url) => {
    const u = new URL(url);
    calls.push(u);
    const json = (body, status = 200) => ({ ok: status === 200, status, json: async () => body });
    if (fail) return json({ error: true, reason: 'Service unavailable' }, 503);
    if (u.host === 'ipinfo.io') return json({ city: 'Bristol', country: 'GB', loc: '51.4545,-2.5879' });
    if (u.host === 'geocoding-api.open-meteo.com') {
      return json(u.searchParams.get('name') === 'Nowhere' ? {} : { results: [{ name: 'Portland', latitude: 45.52, longitude: -122.68, country_code: 'US' }] });
    }
    const d = u.searchParams.get('start_date');
    return json({ daily: { time: [d], weather_code: [code], temperature_2m_max: [13.6], temperature_2m_min: [7.8] } });
  };
  fn.calls = calls;
  return fn;
}

const AT = new Date(2026, 9, 3, 14, 37);

test('WMO codes map to kinds of sky', () => {
  const cases = { 0: 'sun', 1: 'partly', 2: 'partly', 3: 'cloud', 45: 'fog', 53: 'drizzle', 63: 'rain', 81: 'rain', 73: 'snow', 86: 'snow', 95: 'storm', 99: 'storm' };
  for (const [code, kind] of Object.entries(cases)) assert.equal(weatherKind(Number(code)), kind, code);
});

test('every kind has an icon inside its box', () => {
  for (const kind of Object.keys(KIND_LABELS)) {
    const pts = weatherIcon(kind, { x: 10, y: 20, size: 100 }).flat();
    assert.ok(pts.length > 4, kind);
    for (const [x, y] of pts) {
      assert.ok(Number.isFinite(x) && Number.isFinite(y), kind);
      assert.ok(x >= 10 && x <= 110 && y >= 20 && y <= 120, `${kind}: ${x},${y}`);
    }
  }
});

test('the degree sign is a real glyph', () => {
  assert.ok(measureText('14°', { size: 21 }) > measureText('14', { size: 21 }));
});

test('locates by IP and fetches the day\'s forecast', async () => {
  const fetch = fakeFetch();
  const logs = [];
  const w = createWeather({ fetch, log: (m) => logs.push(m) });
  const f = await w.forecast(AT);
  assert.deepEqual(f, { date: '2026-10-03', code: 61, kind: 'rain', high: 13.6, low: 7.8, units: 'celsius', place: 'Bristol, GB' });
  const q = fetch.calls.at(-1).searchParams;
  assert.equal(q.get('latitude'), '51.4545');
  assert.equal(q.get('longitude'), '-2.5879');
  assert.equal(q.get('start_date'), '2026-10-03');
  assert.equal(q.get('end_date'), '2026-10-03');
  assert.equal(q.get('timezone'), 'auto');
  assert.equal(q.get('temperature_unit'), null);
  assert.match(logs[0], /Bristol, GB \(estimated from IP address/);
});

test('--location is geocoded, with an optional country code', async () => {
  const fetch = fakeFetch();
  const w = createWeather({ fetch, location: 'Portland, us', units: 'fahrenheit' });
  const f = await w.forecast(AT);
  assert.equal(f.place, 'Portland, US');
  const geo = fetch.calls.find((u) => u.host === 'geocoding-api.open-meteo.com').searchParams;
  assert.equal(geo.get('name'), 'Portland');
  assert.equal(geo.get('countryCode'), 'US');
  assert.equal(fetch.calls.at(-1).searchParams.get('temperature_unit'), 'fahrenheit');
  assert.ok(!fetch.calls.some((u) => u.host === 'ipinfo.io'));
});

test('an unknown place gives no weather, and says why', async () => {
  const logs = [];
  const w = createWeather({ fetch: fakeFetch(), location: 'Nowhere', log: (m) => logs.push(m) });
  assert.equal(await w.forecast(AT), null);
  assert.match(logs[0], /couldn't find a place called "Nowhere"/);
});

test('--coords skip the lookups', async () => {
  const fetch = fakeFetch();
  await createWeather({ fetch, coords: [51.45, -2.59] }).forecast(AT);
  assert.equal(fetch.calls.length, 1);
  assert.equal(fetch.calls[0].searchParams.get('longitude'), '-2.59');
});

test('forecasts are cached, and kept when a refresh fails', async () => {
  let fail = false;
  const ok = fakeFetch();
  const bad = fakeFetch({ fail: true });
  const fetch = (url) => (fail ? bad(url) : ok(url));
  const logs = [];
  const w = createWeather({ fetch, coords: [1, 2], maxAgeMs: 0, log: (m) => logs.push(m) });
  assert.equal(w.cached(AT), null);
  const first = await w.forecast(AT);
  assert.equal(w.cached(AT), first);
  fail = true;
  assert.deepEqual(await w.forecast(AT), first); // stale beats nothing
  assert.deepEqual(await w.forecast(AT), first);
  assert.equal(logs.filter((m) => /Service unavailable/.test(m)).length, 1); // logged once
  assert.equal(await w.forecast(new Date(2026, 9, 4, 9)), null); // a new day with no data
});

test('a fresh forecast is reused rather than refetched', async () => {
  const fetch = fakeFetch();
  const w = createWeather({ fetch, coords: [1, 2] });
  await Promise.all([w.forecast(AT), w.forecast(AT)]);
  await w.forecast(new Date(2026, 9, 3, 15));
  assert.equal(fetch.calls.length, 1);
  assert.equal(localDate(AT), '2026-10-03');
});

test('the header fits weather on every panel without touching the margins', () => {
  const extremes = [{ kind: 'storm', high: 104, low: 88 }, { kind: 'snow', high: -12, low: -23 }, { kind: 'partly', high: 3, low: -1 }];
  for (const p of Object.values(PRESETS)) {
    for (const [i, weather] of extremes.entries()) {
      const { bitmap } = renderFrame(new Date(2026, 8, 30, 22, 45), { ...p, weather, clock: i ? '24h' : '12h' });
      const m = 3;
      for (let y = 0; y < bitmap.height; y++) {
        for (let x = 0; x < bitmap.width; x++) {
          if (x < m || y < m || x >= bitmap.width - m || y >= bitmap.height - m) {
            assert.equal(bitmap.get(x, y), 0, `${p.label}: ink at ${x},${y}`);
          }
        }
      }
    }
  }
});

test('weather changes the header and nothing else', () => {
  const plain = renderFrame(AT).bitmap;
  const wet = renderFrame(AT, { weather: { kind: 'rain', high: 14, low: 8 } }).bitmap;
  let changedBelow = 0;
  let changedAbove = 0;
  for (let i = 0; i < plain.data.length; i++) {
    if (plain.data[i] !== wet.data[i]) (Math.floor(i / plain.width) < 80 ? changedAbove++ : changedBelow++);
  }
  assert.ok(changedAbove > 100);
  assert.equal(changedBelow, 0);
});
