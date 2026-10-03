// Silly fish name generator.
//
// generateCatch(seed) returns { name, note, rare, style } for a seed string.
// The name is what fishdraw is seeded with, so a name always draws the same
// fish — try any of them in the fishdraw demo and you'll get the same one.

import { makeRng } from './rng.js';

export const MAX_NAME_LENGTH = 32;

// ---------------------------------------------------------------------------
// Mock-Latin binomials: "Wobblichthys bewilderus"

const GENUS_ROOTS = [
  'Blub', 'Gloop', 'Wobbl', 'Splosh', 'Burbl', 'Glub', 'Flopp', 'Snorkl',
  'Squelch', 'Dribbl', 'Noodl', 'Plop', 'Sogg', 'Gulp', 'Bonk', 'Waffl',
  'Doodl', 'Squish', 'Floof', 'Fumbl', 'Grumbl', 'Snack', 'Bloat', 'Wibbl',
  'Smoosh', 'Bamboozl', 'Dingl', 'Puddl', 'Muddl', 'Kerfuffl', 'Blorp',
  'Splat', 'Snooz', 'Faff', 'Crumpet', 'Biscu', 'Gobbl', 'Hiccup',
];

const GENUS_SUFFIXES = [
  'ichthys', 'odon', 'osaurus', 'opterus', 'ops', 'ella', 'ius', 'us',
  'onyx', 'ostomus', 'ocephalus', 'orhynchus', 'ichthyoides', 'ognathus',
];

const SPECIES = [
  'maximus', 'minimus', 'ridiculosus', 'confusus', 'magnificus', 'absurdus',
  'dramaticus', 'nonchalantus', 'bewilderus', 'sogginus', 'flappus',
  'gigglus', 'grumpius', 'snoozus', 'smugglerus', 'wobblus', 'derpus',
  'flamboyantus', 'pompous', 'soggybottomus', 'perplexus', 'unimpressus',
  'snackii', 'sploshii', 'garyi', 'kevinii', 'brendae', 'nigelii',
  'barbarae', 'derekii', 'puddlensis', 'bathtubensis', 'fridgensis',
  'sofaensis', 'kettlensis', 'carparkensis', 'wellingtonii', 'biscuitivorus',
  'crispivorus', 'toastivorus', 'mondayensis', 'loudmouthus', 'yodelensis',
];

// ---------------------------------------------------------------------------
// Field-guide common names: "Lesser Spotted Disco Halibut"

const QUALIFIERS = [
  'Lesser', 'Greater', 'Common', 'Northern', 'Southern', 'Lesser Spotted',
  'Pygmy', 'Giant', 'Royal', 'Bearded', 'Wild', 'Domestic', 'Suburban',
  'Occasional', 'Reluctant', 'Part-time', 'Semi-retired', 'Inland', 'Feral',
  'Unexpected', 'Freelance', 'Municipal', 'Ornamental', 'Moderately Rare',
];

const ADJECTIVES = [
  'Grumpy', 'Bewildered', 'Sleepy', 'Disco', 'Soggy', 'Wobbly', 'Dramatic',
  'Smug', 'Anxious', 'Flamboyant', 'Chunky', 'Startled', 'Damp',
  'Sarcastic', 'Philosophical', 'Overcaffeinated', 'Sneaky', 'Jazz',
  'Velvet', 'Haunted', 'Retired', 'Confused', 'Bashful', 'Sassy', 'Polite',
  'Moody', 'Unimpressed', 'Hungover', 'Glamorous', 'Suspicious', 'Fancy',
  'Tiny', 'Enormous', 'Pompous', 'Gormless', 'Cheeky',
];

const FISH = [
  'Haddock', 'Halibut', 'Mackerel', 'Sardine', 'Flounder', 'Herring',
  'Guppy', 'Trout', 'Catfish', 'Pufferfish', 'Anchovy', 'Grouper',
  'Sturgeon', 'Turbot', 'Pollock', 'Kipper', 'Minnow', 'Goldfish',
  'Barracuda', 'Blobfish', 'Wrasse', 'Carp', 'Cod', 'Pike', 'Perch',
  'Bream', 'Tench', 'Gudgeon', 'Pilchard', 'Snapper', 'Monkfish', 'Plaice',
];

const COMPOUND_FRONT = [
  'Toast', 'Sock', 'Biscuit', 'Puddle', 'Noodle', 'Trouser', 'Spoon',
  'Kettle', 'Waffle', 'Teacup', 'Sausage', 'Pickle', 'Pudding', 'Crumpet',
  'Banjo', 'Cardigan', 'Doughnut', 'Sofa', 'Wheelbarrow', 'Muffin',
  'Moustache', 'Spatula', 'Teapot', 'Slipper', 'Custard', 'Trumpet',
  'Umbrella', 'Jelly', 'Tuba', 'Bucket',
];

const COMPOUND_BACK = [
  'fin', 'gill', 'fish', 'cod', 'ray', 'eel', 'mouth', 'belly', 'snapper',
  'tail', 'nose', 'jaw', 'scale', 'sprat', 'pike', 'perch', 'bream',
];

const BODY_PARTS = ['nosed', 'bellied', 'finned', 'faced', 'headed', 'tailed', 'lipped', 'eyed'];

// ---------------------------------------------------------------------------
// Characters: "Captain Brenda Barnaclewick", "Keith the Halibut"

const TITLES = [
  'Sir', 'Lady', 'Captain', 'Professor', 'Doctor', 'Lord', 'Baroness',
  'Admiral', 'Count', 'Duchess', 'Uncle', 'Auntie', 'Madame', 'Little',
  'Big', 'Old', 'Detective', 'Chef', 'DJ', 'Grand Duke', 'Nan', 'Coach',
  'Sergeant', 'Mayor', 'Archduke', 'Dame',
];

const FIRST_NAMES = [
  'Reginald', 'Gary', 'Kevin', 'Brenda', 'Nigel', 'Barbara', 'Derek',
  'Philippa', 'Clive', 'Gerald', 'Doris', 'Trevor', 'Mildred', 'Colin',
  'Beryl', 'Keith', 'Maureen', 'Wendy', 'Norman', 'Agnes', 'Bernard',
  'Gladys', 'Dave', 'Susan', 'Terry', 'Shirley', 'Howard', 'Phyllis',
  'Roger', 'Ethel', 'Graham', 'Pauline', 'Malcolm', 'Brian', 'Linda',
  'Neville', 'Hilda', 'Rodney', 'Cynthia', 'Barry',
];

const SURNAME_FRONT = [
  'Bubble', 'Flounder', 'Wobble', 'Gill', 'Fin', 'Splash', 'Scale',
  'Kipper', 'Haddock', 'Sprat', 'Puddle', 'Blub', 'Squid', 'Brine',
  'Plankton', 'Ripple', 'Barnacle', 'Pickle', 'Soggy', 'Trout', 'Wiggle',
  'Splosh', 'Guppy', 'Seaweed', 'Bilge', 'Pebble',
];

const SURNAME_BACK = [
  'bottom', 'worth', 'sworth', 'ington', 'ley', 'ton', 'smith', 'wick',
  'thwaite', 'shaw', 'field', 'by', 'ham', 'stone', 'more', 'bury',
  'wallop', 'botham', 'face', 'pants', 'sby', 'hampton',
];

const PARTICLES = ['von', 'de la', 'Mc', "O'", 'van der'];

// ---------------------------------------------------------------------------
// Legendary catches. Rare: roughly one hour in twenty-five.

const LEGENDS = [
  'The Codfather', 'Cod Almighty', 'Bass Lightyear', 'Krill Bill',
  'Carpe Diem', 'Sole Survivor', 'Fishy McFishface', 'Tuna Turner',
  'Squid Pro Quo', 'Eel of Fortune', 'Prawn of the Dead', 'Hake Expectations',
  'Plaice Invaders', 'Shrimply the Best', 'Codzilla', 'Johann Sebastian Bass',
  'Salmon Chanted Evening', 'The Great Gill', 'Pike Speed', 'Fin Diesel',
  'Oh My Cod', 'The Bream Team', 'Finding Emo', 'Holy Mackerel',
];

// ---------------------------------------------------------------------------
// Field notes: one line of entirely accurate science.

const NOTES = {
  Habitat: [
    'the back of the fridge', 'a forgotten paddling pool', 'the deep end',
    'a damp sock drawer', 'under the pier', "Grandma's bathtub",
    'the office water cooler', 'a very small puddle', 'the lost property bin',
    'the Mariana Trench (weekends only)', 'a soggy cardboard box',
    'the gap behind the radiator', 'a bucket of questionable origin',
    'the third pothole on the left', 'a teapot, reluctantly',
    'the shallow end of a motorway services', 'inside a wellington boot',
  ],
  Diet: [
    'mostly crisps', "other people's chips", 'plankton and gossip',
    'soggy toast', 'exclusively blue things', 'bread meant for the ducks',
    'loose change', 'custard creams', 'cold baked beans', 'regret',
    'whatever is in the bottom of the bag', 'fish fingers (ethically complex)',
    'small talk', 'the last biscuit',
  ],
  'Known for': [
    'aggressive yodelling', 'losing at chess', 'never returning library books',
    'a surprisingly firm handshake', 'interpretive dance',
    'telling the same joke twice', 'pretending to be a rock',
    'complaining about the water temperature', 'excellent posture',
    'refusing to use the bubbles provided', 'karaoke (badly)',
    'remembering everyone\'s birthday', 'unsolicited life advice',
    'swimming in circles, on purpose',
  ],
  Temperament: [
    'deeply suspicious', 'mildly damp', 'wildly overconfident',
    'dramatic but fair', 'cheerfully confused', 'perpetually sleepy',
    'polite to a fault', 'easily startled by spoons', 'quietly smug',
    'chaotic neutral', 'in need of a little sit down',
  ],
  'Conservation status': [
    'Least Bothered', 'Mostly Fine', 'Critically Smug', 'Fashionably Late',
    'Vulnerable to Puns', 'Thriving, Apparently', 'Locally Notorious',
    'Under Review (by a cat)', 'Extremely Online',
  ],
};

// ---------------------------------------------------------------------------

function binomial(r) {
  return `${r.pick(GENUS_ROOTS)}${r.pick(GENUS_SUFFIXES)} ${r.pick(SPECIES)}`;
}

function compound(r) {
  return `${r.pick(COMPOUND_FRONT)}${r.pick(COMPOUND_BACK)}`;
}

function commonName(r) {
  return r.weighted([
    [() => `${r.pick(QUALIFIERS)} ${r.pick(ADJECTIVES)} ${r.pick(FISH)}`, 3],
    [() => `${r.pick(QUALIFIERS)} ${compound(r)}`, 2],
    [() => `${r.pick(ADJECTIVES)} ${compound(r)}`, 2],
    [() => `${r.pick(COMPOUND_FRONT)}-${r.pick(BODY_PARTS)} ${r.pick(FISH)}`, 2],
    [() => `${r.pick(ADJECTIVES)} ${r.pick(FISH)}`, 1],
  ])();
}

function surname(r) {
  return `${r.pick(SURNAME_FRONT)}${r.pick(SURNAME_BACK)}`;
}

function character(r) {
  return r.weighted([
    [() => `${r.pick(TITLES)} ${r.pick(FIRST_NAMES)} ${surname(r)}`, 3],
    [() => `${r.pick(TITLES)} ${surname(r)}`, 2],
    [() => {
      const p = r.pick(PARTICLES);
      const glue = p.endsWith("'") || p === 'Mc' ? '' : ' ';
      return `${r.pick(FIRST_NAMES)} ${p}${glue}${surname(r)}`;
    }, 2],
    [() => `${r.pick(FIRST_NAMES)} the ${r.pick(FISH)}`, 2],
    [() => `${r.pick(FIRST_NAMES)} the ${r.pick(ADJECTIVES)} ${r.pick(FISH)}`, 2],
  ])();
}

function note(r) {
  const field = r.pick(Object.keys(NOTES));
  return `${field}: ${r.pick(NOTES[field])}`;
}

export function generateCatch(seed) {
  const r = makeRng(`catch:${seed}`);
  const rare = r.chance(0.04);
  let style, name;
  if (rare) {
    style = 'legend';
    name = r.pick(LEGENDS);
  } else {
    // Re-roll anything too long to sit nicely under the fish.
    do {
      style = r.weighted([['binomial', 3], ['common', 4], ['character', 4]]);
      name = style === 'binomial' ? binomial(r) : style === 'common' ? commonName(r) : character(r);
    } while (name.length > MAX_NAME_LENGTH);
  }
  return { name, note: note(r), rare, style };
}
