// The browser simulator. It runs the exact renderer the device uses, then
// paints the 1-bit frame onto a canvas styled like an e-ink panel.

import { renderFrame, PRESETS } from '../src/render.js';
import { hourKey, startOfHour, catchesFrom } from '../src/schedule.js';
import { KIND_LABELS, SAMPLES, toFahrenheit } from '../src/weather.js';
import { createWeather, localDate } from '../src/forecast.js';

const HOUR = 3600_000;
const FF_INTERVAL = 4000;
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)');

const $ = (id) => document.getElementById(id);
const canvas = $('panel');
const ctx = canvas.getContext('2d');

const SAVED = ['preset', 'portrait', 'clock', 'update', 'salt', 'weather', 'place', 'units'];
const state = {
  offset: 0, // simulated time = real time + offset
  ff: false,
  preset: 'waveshare-7in5',
  portrait: false,
  clock: '24h',
  update: 'minute',
  salt: '',
  weather: 'forecast', // 'forecast', 'none', or a sample kind from SAMPLES
  place: 'London, GB', // where the simulated frame is
  units: 'c',
};
try {
  const saved = JSON.parse(localStorage.getItem('fish-fodder') ?? '{}');
  for (const k of SAVED) if (k in saved) state[k] = saved[k];
  if (!PRESETS[state.preset]) state.preset = 'waveshare-7in5';
  if (state.weather === 'live') state.weather = 'forecast'; // older saved setting
} catch {}
function save() {
  try {
    localStorage.setItem('fish-fodder', JSON.stringify(Object.fromEntries(SAVED.map((k) => [k, state[k]]))));
  } catch {}
}

const simNow = () => new Date(Date.now() + state.offset);

function size() {
  const p = PRESETS[state.preset];
  return state.portrait ? { width: p.height, height: p.width } : { width: p.width, height: p.height };
}
const settingsKey = (weather) => JSON.stringify([state.preset, state.portrait, state.clock, state.update, state.salt, weather]);

// ---------------------------------------------------------------------------
// Weather. The simulator looks up the forecast for a location you choose,
// the same way the frame does for its own. Where the page can't reach the
// internet (the published demo), it offers sample weather instead.

let forecastAvailable = true;
let networkFailed = false;
const simFetch = (url, opts) => fetch(url, opts).catch((err) => {
  networkFailed = true;
  throw err;
});

const services = new Map(); // place → { svc, message }
const tried = new Set(); // place|date pairs already fetched (or fetching)
const pending = new Set();

// "51.45,-2.59" → coordinates, anything else → a place name.
function placeOptions(text) {
  const m = text.match(/^\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*$/);
  return m ? { coords: [Number(m[1]), Number(m[2])] } : { location: text };
}

// One forecast client per place, so going back to a place reuses its forecast.
function placeEntry() {
  if (!state.place) return null;
  if (!services.has(state.place)) {
    const entry = { message: '' };
    entry.svc = createWeather({ ...placeOptions(state.place), fetch: simFetch, log: (msg) => { entry.message = msg.replace(/^weather: /, ''); } });
    services.set(state.place, entry);
  }
  return services.get(state.place);
}
const service = () => placeEntry()?.svc ?? null;

// Fetch the forecast for `now`'s date once; resolves when it has settled.
function requestForecast(now) {
  const s = service();
  const key = `${state.place}|${localDate(now)}`;
  if (!s || tried.has(key)) return Promise.resolve();
  tried.add(key);
  pending.add(key);
  return s.forecast(now).then(() => {
    pending.delete(key);
    if (networkFailed && !s.cached(now)) {
      forecastAvailable = false;
      weatherOptions();
      syncButtons();
    }
    tick();
  });
}

function convert(c, from, to) {
  if (from === to) return c;
  return to === 'f' ? toFahrenheit(c) : ((c - 32) * 5) / 9;
}

// The weather choice in effect: no forecast without the internet, so a sample.
const weatherMode = () => (state.weather === 'forecast' && !forecastAvailable ? 'partly' : state.weather);

// The weather to draw now, or null.
function weatherNow(now) {
  const mode = weatherMode();
  if (mode === 'none') return null;
  if (mode === 'forecast') {
    const w = service()?.cached(now);
    if (!w) {
      requestForecast(now);
      return null;
    }
    return { kind: w.kind, high: convert(w.high, 'c', state.units), low: convert(w.low, 'c', state.units) };
  }
  const kind = SAMPLES[mode] ? mode : 'partly';
  const [high, low] = SAMPLES[kind];
  return { kind, high: convert(high, 'c', state.units), low: convert(low, 'c', state.units) };
}

function weatherStatus(now) {
  const mode = weatherMode();
  if (mode === 'none') return '';
  if (mode !== 'forecast') return `Sample weather: ${KIND_LABELS[mode] ?? KIND_LABELS.partly}`;
  if (!state.place) return 'Enter a location for the forecast';
  const entry = placeEntry();
  if (entry.svc.cached(now)) return `Forecast for ${entry.svc.place()?.name ?? state.place}`;
  if (pending.has(`${state.place}|${localDate(now)}`)) return 'Fetching forecast…';
  return entry.message ? `No forecast: ${entry.message}` : 'No forecast for this day';
}

// ---------------------------------------------------------------------------
// Painting

const css = getComputedStyle(document.documentElement);
const rgb = (name) => {
  const c = document.createElement('canvas').getContext('2d');
  c.fillStyle = css.getPropertyValue(name).trim();
  const hex = c.fillStyle; // normalised to #rrggbb
  return [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
};
const PAPER = rgb('--paper');
const INK = rgb('--ink');

function fitCanvas({ width, height }) {
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const k = Math.max(width, height) < 500 ? 1.6 : 1; // show tiny panels a bit bigger
  if (height > width) {
    canvas.style.width = 'auto';
    canvas.style.height = `min(68vh, ${height * k}px, calc((100vw - 150px) * ${height / width}))`;
  } else {
    canvas.style.height = 'auto';
    canvas.style.width = `${width * k}px`;
  }
}

function fill(color) {
  ctx.fillStyle = `rgb(${color.join(',')})`;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
}

function paint(bitmap) {
  const img = ctx.createImageData(bitmap.width, bitmap.height);
  for (let i = 0; i < bitmap.data.length; i++) {
    const c = bitmap.data[i] ? INK : PAPER;
    img.data.set(c, i * 4);
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

// A full e-ink refresh flashes black and white a few times before the new
// image settles; the fish is drawn during the first black flash.
async function fullRefresh(render) {
  if (reduceMotion.matches) return paint(render());
  fill(INK);
  await wait(30);
  const bitmap = render();
  await wait(120);
  fill(PAPER);
  await wait(160);
  fill(INK);
  await wait(160);
  paint(bitmap);
}

// ---------------------------------------------------------------------------
// The clock loop

const shown = { frame: null, hour: null, settings: null };
let busy = false;
let lastMode = 'full';

function frameKey(d) {
  return state.update === 'hour' ? hourKey(d) : `${hourKey(d)}:${d.getMinutes()}`;
}

let ready = false; // set once the first forecast is in (or has timed out)

async function tick() {
  if (!ready) return;
  const now = simNow();
  updateStatus(now);
  if (busy) return;
  const weather = weatherNow(now);
  const settings = settingsKey(weather);
  const hour = hourKey(now);
  if (frameKey(now) === shown.frame && settings === shown.settings) return;

  const full = hour !== shown.hour || settings !== shown.settings;
  const sz = size();
  const render = () => renderFrame(now, { ...sz, clock: state.clock, precision: state.update, salt: state.salt, weather }).bitmap;
  Object.assign(shown, { frame: frameKey(now), hour, settings });
  busy = true;
  try {
    fitCanvas(sz);
    if (full) await fullRefresh(render);
    else paint(render());
    lastMode = full ? 'full' : 'partial';
  } finally {
    busy = false;
  }
  updateStatus(simNow());
  updateLog(now);
}

function updateStatus(now) {
  $('s-refresh').textContent = lastMode === 'full' ? 'Last update: full refresh' : 'Last update: partial refresh';
  const mins = 60 - now.getMinutes();
  $('s-next').textContent = state.ff ? 'a few seconds' : mins === 60 ? 'an hour' : `${mins} min`;
  const p = PRESETS[state.preset];
  const { width, height } = size();
  $('s-panel').textContent = `${width}×${height} · ${p.label.replace(/\s*\(.*\)$/, '')}`;
  $('s-weather').textContent = weatherStatus(now);

  const at = $('t-at');
  if (document.activeElement !== at) {
    const pad = (n) => String(n).padStart(2, '0');
    at.value = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}T${pad(now.getHours())}:${pad(now.getMinutes())}`;
  }
  $('t-live').setAttribute('aria-pressed', String(state.offset === 0 && !state.ff));
  $('t-ff').setAttribute('aria-pressed', String(state.ff));
}

// ---------------------------------------------------------------------------
// The day's catch

let logDay = null;
function updateLog(now) {
  const dayStart = new Date(now);
  dayStart.setHours(0, 0, 0, 0);
  const key = `${dayStart.toDateString()}|${state.salt}`;
  if (key !== logDay) {
    logDay = key;
    $('log-day').textContent = `${dayStart.toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' })}. Pick an hour to see that fish.`;
    $('log').replaceChildren(
      ...catchesFrom(dayStart, 24, { salt: state.salt }).map((c) => {
        const li = document.createElement('li');
        li.dataset.hour = c.key;
        const b = document.createElement('button');
        b.type = 'button';
        b.innerHTML = '<span class="hh"></span><span class="nm"></span><span class="tag"></span>';
        b.querySelector('.hh').textContent = `${String(c.at.getHours()).padStart(2, '0')}:00`;
        b.querySelector('.nm').textContent = c.name;
        b.querySelector('.tag').textContent = c.rare ? 'Rare' : '';
        b.title = c.note;
        b.addEventListener('click', () => travelTo(c.at));
        li.append(b);
        return li;
      }),
    );
  }
  const hour = hourKey(now);
  for (const li of $('log').children) li.classList.toggle('now', li.dataset.hour === hour);
}

// ---------------------------------------------------------------------------
// Controls

let ffTimer = null;
function setFF(on) {
  state.ff = on;
  clearInterval(ffTimer);
  if (on) {
    const step = () => {
      state.offset = startOfHour(simNow()).getTime() + HOUR - Date.now();
      tick();
    };
    step();
    ffTimer = setInterval(step, FF_INTERVAL);
  }
  tick();
}

function travelTo(date) {
  setFF(false);
  state.offset = date.getTime() - Date.now();
  tick();
}

function setting(key, value) {
  state[key] = value;
  save();
  syncButtons();
  tick();
}

function syncButtons() {
  const pressed = { 'p-land': !state.portrait, 'p-port': state.portrait, 'c-24': state.clock === '24h', 'c-12': state.clock === '12h', 'u-min': state.update === 'minute', 'u-hour': state.update === 'hour', 'w-c': state.units === 'c', 'w-f': state.units === 'f' };
  for (const [id, on] of Object.entries(pressed)) $(id).setAttribute('aria-pressed', String(on));
  $('p-preset').value = state.preset;
  $('f-salt').value = state.salt;
  $('w-mode').value = weatherMode();
  $('w-place').value = state.place;
  $('w-place').hidden = weatherMode() !== 'forecast';
}

$('p-preset').append(...Object.entries(PRESETS).map(([k, p]) => new Option(p.label, k)));
$('t-live').addEventListener('click', () => { setFF(false); state.offset = 0; tick(); });
$('t-ff').addEventListener('click', () => setFF(!state.ff));
$('t-prev').addEventListener('click', () => travelTo(new Date(simNow().getTime() - HOUR)));
$('t-next').addEventListener('click', () => travelTo(new Date(simNow().getTime() + HOUR)));
$('t-at').addEventListener('change', (e) => {
  const d = new Date(e.target.value);
  if (!Number.isNaN(d.getTime())) travelTo(d);
});
$('p-preset').addEventListener('change', (e) => setting('preset', e.target.value));
$('p-land').addEventListener('click', () => setting('portrait', false));
$('p-port').addEventListener('click', () => setting('portrait', true));
$('c-24').addEventListener('click', () => setting('clock', '24h'));
$('c-12').addEventListener('click', () => setting('clock', '12h'));
$('u-min').addEventListener('click', () => setting('update', 'minute'));
$('u-hour').addEventListener('click', () => setting('update', 'hour'));
$('w-mode').addEventListener('change', (e) => setting('weather', e.target.value));
$('w-place').addEventListener('change', (e) => setting('place', e.target.value.trim()));
$('w-c').addEventListener('click', () => setting('units', 'c'));
$('w-f').addEventListener('click', () => setting('units', 'f'));
$('f-salt').addEventListener('change', (e) => setting('salt', e.target.value.trim()));
$('controls').addEventListener('submit', (e) => e.preventDefault());

function weatherOptions() {
  const opts = Object.keys(SAMPLES).map((k) => new Option(`Sample: ${KIND_LABELS[k]}`, k));
  if (forecastAvailable) opts.unshift(new Option('Forecast for a location', 'forecast'));
  opts.push(new Option('No weather', 'none'));
  $('w-mode').replaceChildren(...opts);
  $('w-hint').textContent = forecastAvailable
    ? "The frame shows today's forecast for where it is. Type any place to pretend it's there, or pick a sample."
    : "The frame shows today's forecast for where it is. This page can't fetch forecasts, so it shows samples. Run npm run sim for real ones.";
}

// Hold the first draw (briefly) for the forecast, to avoid two refreshes.
weatherOptions();
const firstForecast = weatherMode() === 'forecast' ? requestForecast(simNow()) : Promise.resolve();
Promise.race([firstForecast, wait(4000)]).finally(() => {
  ready = true;
  tick();
});

syncButtons();
fitCanvas(size());
fill(PAPER);
tick();
setInterval(tick, 1000);
