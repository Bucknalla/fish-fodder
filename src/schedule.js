// Which fish is on duty when. A catch is keyed by the local hour, so every
// frame (and the simulator) agrees on who's swimming at 3pm today.

import { generateCatch } from './names.js';

const pad = (n) => String(n).padStart(2, '0');

export function hourKey(date) {
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}`;
}

export function startOfHour(date) {
  const d = new Date(date);
  d.setMinutes(0, 0, 0);
  return d;
}

// `salt` lets two frames on the same wall show different fish.
export function catchFor(date, { salt = '' } = {}) {
  const key = hourKey(date);
  return { key, ...generateCatch(salt ? `${salt}/${key}` : key) };
}

// The catches for the `count` hours starting at `date`'s hour.
export function catchesFrom(date, count, opts) {
  const start = startOfHour(date);
  return Array.from({ length: count }, (_, i) => {
    const at = new Date(start);
    at.setHours(start.getHours() + i);
    return { at, ...catchFor(at, opts) };
  });
}
