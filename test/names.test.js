import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateCatch, MAX_NAME_LENGTH } from '../src/names.js';
import { catchFor, catchesFrom, hourKey } from '../src/schedule.js';

test('the same seed always gives the same catch', () => {
  assert.deepEqual(generateCatch('2026-10-03T14'), generateCatch('2026-10-03T14'));
});

test('names are printable, fit the label, and vary', () => {
  const names = new Set();
  let rare = 0;
  for (let i = 0; i < 5000; i++) {
    const c = generateCatch(`seed ${i}`);
    assert.match(c.name, /^[\x20-\x7e]+$/, c.name); // the Hershey fonts cover ASCII 32..126
    assert.match(c.note, /^[\x20-\x7e]+$/, c.note);
    assert.ok(c.name.length <= MAX_NAME_LENGTH, c.name);
    assert.equal(c.name, c.name.trim());
    names.add(c.name);
    rare += c.rare;
  }
  assert.ok(names.size > 4500, `only ${names.size} distinct names`);
  assert.ok(rare > 100 && rare < 400, `${rare} rare catches in 5000`);
});

test('names only promise fish that fishdraw can draw', () => {
  // fishdraw draws ordinary finned fish: no eels, rays, flatfish, puffers or
  // shellfish. Whole words only, so surnames like Flounderbottom are fine.
  const wrong = /\b(eel|ray|squid|octopus|prawn|shrimp|krill|crab|lobster|halibut|flounder|plaice|sole|turbot|pufferfish|blobfish|monkfish|seahorse|jellyfish|starfish)s?\b/i;
  for (let i = 0; i < 20000; i++) {
    const { name } = generateCatch(`seed ${i}`);
    assert.doesNotMatch(name, wrong, name);
  }
});

test('a catch is keyed by the local hour', () => {
  const a = catchFor(new Date(2026, 9, 3, 14, 1));
  const b = catchFor(new Date(2026, 9, 3, 14, 59));
  const c = catchFor(new Date(2026, 9, 3, 15, 0));
  assert.equal(a.key, '2026-10-03T14');
  assert.equal(a.name, b.name);
  assert.notEqual(a.key, c.key);
});

test('salt gives a different schedule', () => {
  const at = new Date(2026, 9, 3, 9);
  const differs = Array.from({ length: 24 }, (_, h) => {
    at.setHours(h);
    return catchFor(at).name !== catchFor(at, { salt: 'kitchen' }).name;
  });
  assert.ok(differs.filter(Boolean).length > 20);
});

test('catchesFrom lists consecutive hours', () => {
  const list = catchesFrom(new Date(2026, 9, 3, 22, 30), 4);
  assert.deepEqual(list.map((c) => c.key), ['2026-10-03T22', '2026-10-03T23', '2026-10-04T00', '2026-10-04T01']);
  assert.equal(list[2].name, catchFor(new Date(2026, 9, 4, 0, 45)).name);
  assert.equal(hourKey(list[0].at), '2026-10-03T22');
});
