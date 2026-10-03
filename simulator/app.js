// The browser simulator. It runs the exact renderer the device uses, then
// paints the 1-bit frame onto a canvas styled like an e-ink panel.

import { renderFrame, PRESETS } from '../src/render.js';
import { hourKey, startOfHour, catchesFrom } from '../src/schedule.js';

const HOUR = 3600_000;
const FF_INTERVAL = 4000;
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)');

const $ = (id) => document.getElementById(id);
const canvas = $('panel');
const ctx = canvas.getContext('2d');

const SAVED = ['preset', 'portrait', 'clock', 'update', 'salt'];
const state = {
  offset: 0, // simulated time = real time + offset
  ff: false,
  preset: 'waveshare-7in5',
  portrait: false,
  clock: '24h',
  update: 'minute',
  salt: '',
};
try {
  const saved = JSON.parse(localStorage.getItem('fish-fodder') ?? '{}');
  for (const k of SAVED) if (k in saved) state[k] = saved[k];
  if (!PRESETS[state.preset]) state.preset = 'waveshare-7in5';
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
const settingsKey = () => JSON.stringify([state.preset, state.portrait, state.clock, state.update, state.salt]);

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

async function tick() {
  const now = simNow();
  updateStatus(now);
  if (busy) return;
  const settings = settingsKey();
  const hour = hourKey(now);
  if (frameKey(now) === shown.frame && settings === shown.settings) return;

  const full = hour !== shown.hour || settings !== shown.settings;
  const sz = size();
  const render = () => renderFrame(now, { ...sz, clock: state.clock, precision: state.update, salt: state.salt }).bitmap;
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
  const pressed = { 'p-land': !state.portrait, 'p-port': state.portrait, 'c-24': state.clock === '24h', 'c-12': state.clock === '12h', 'u-min': state.update === 'minute', 'u-hour': state.update === 'hour' };
  for (const [id, on] of Object.entries(pressed)) $(id).setAttribute('aria-pressed', String(on));
  $('p-preset').value = state.preset;
  $('f-salt').value = state.salt;
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
$('f-salt').addEventListener('change', (e) => setting('salt', e.target.value.trim()));
$('controls').addEventListener('submit', (e) => e.preventDefault());

syncButtons();
fitCanvas(size());
fill(PAPER);
tick();
setInterval(tick, 1000);
