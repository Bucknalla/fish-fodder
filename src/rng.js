// Small seeded PRNG helpers. Everything random in fish-fodder flows from a
// string seed, so the same hour always produces the same catch.

// FNV-1a, 32-bit.
export function hashString(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

// mulberry32: tiny, fast, good enough for picking words.
function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function makeRng(seed) {
  const next = mulberry32(typeof seed === 'string' ? hashString(seed) : seed);
  const rng = {
    next,
    int: (n) => Math.floor(next() * n),
    chance: (p) => next() < p,
    pick: (arr) => arr[Math.floor(next() * arr.length)],
    // Pick from [[item, weight], ...].
    weighted(entries) {
      const total = entries.reduce((s, [, w]) => s + w, 0);
      let r = next() * total;
      for (const [item, w] of entries) {
        if ((r -= w) < 0) return item;
      }
      return entries[entries.length - 1][0];
    },
  };
  return rng;
}
