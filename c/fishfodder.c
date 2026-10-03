// fish-fodder's clock face in C. A port of src/rng.js, src/names.js,
// src/schedule.js, src/text.js, src/weather.js (the icons), src/raster.js and
// src/render.js, producing the same frames pixel for pixel. The fish comes
// from fishdraw.c. Compile with -ffp-contract=off (see c/Makefile).

#include "fishfodder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fdlibm.h"
#include "fishdraw.h"
#include "fishfodder_data.h"

#define PI 3.141592653589793
#define TAU (PI * 2)

// ---------------------------------------------------------------------------
// JavaScript number semantics

static double js_min(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == 0 && b == 0) return signbit(a) ? a : b;
  return a < b ? a : b;
}
static double js_max(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == 0 && b == 0) return signbit(a) ? b : a;
  return a > b ? a : b;
}
// Math.round: the nearest integer, halves rounding up.
static double js_round(double x) {
  if (!isfinite(x)) return x;
  double f = floor(x);
  return x - f >= 0.5 ? f + 1 : f;
}

// UTF-8 to the UTF-16 code units JavaScript strings are made of.
static int utf16_units(const char *s, uint16_t *out, int max) {
  const unsigned char *u = (const unsigned char *)s;
  int n = 0;
  while (*u && n < max - 1) {
    uint32_t cp;
    if (*u < 0x80) cp = *u++;
    else if ((*u & 0xE0) == 0xC0 && u[1]) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
    else if ((*u & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
    else if ((*u & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
      cp = ((uint32_t)(u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
      u += 4;
    } else { cp = 0xFFFD; u++; }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out[n++] = 0xD800 + (cp >> 10);
      out[n++] = 0xDC00 + (cp & 0x3FF);
    } else {
      out[n++] = cp;
    }
  }
  return n;
}

// Next code point of a UTF-8 string (for drawing text).
static uint32_t next_cp(const char **s) {
  const unsigned char *u = (const unsigned char *)*s;
  uint32_t cp;
  if (*u < 0x80) { cp = *u; *s += 1; }
  else if ((*u & 0xE0) == 0xC0 && u[1]) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); *s += 2; }
  else if ((*u & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); *s += 3; }
  else if ((*u & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
    cp = ((uint32_t)(u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    *s += 4;
  } else { cp = 0xFFFD; *s += 1; }
  return cp;
}

// ---------------------------------------------------------------------------
// rng.js

static uint32_t hash_string(const char *s) { // FNV-1a over UTF-16 units
  uint16_t units[640];
  int n = utf16_units(s, units, 640);
  uint32_t h = 0x811c9dc5u;
  for (int i = 0; i < n; i++) {
    h ^= units[i];
    h *= 0x01000193u;
  }
  return h;
}

typedef struct { uint32_t a; } Rng;

static double rng_next(Rng *r) { // mulberry32
  r->a += 0x6d2b79f5u;
  uint32_t t = r->a;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (double)(t ^ (t >> 14)) / 4294967296.0;
}
static int rng_index(Rng *r, int n) { return (int)floor(rng_next(r) * n); }
static int rng_weighted(Rng *r, const double *w, int n) {
  double total = 0;
  for (int i = 0; i < n; i++) total += w[i];
  double x = rng_next(r) * total;
  for (int i = 0; i < n; i++)
    if ((x -= w[i]) < 0) return i;
  return n - 1;
}
#define PICK(r, arr) (arr)[rng_index((r), COUNT(arr))]

// ---------------------------------------------------------------------------
// names.js

#define MAX_NAME_LENGTH 32

typedef struct {
  char s[160];
  int n;
} Str;
static void sadd(Str *b, const char *t) {
  while (*t && b->n < (int)sizeof b->s - 1) b->s[b->n++] = *t++;
  b->s[b->n] = 0;
}

static void compound(Rng *r, Str *b) {
  sadd(b, PICK(r, COMPOUND_FRONT));
  sadd(b, PICK(r, COMPOUND_BACK));
}
static void surname(Rng *r, Str *b) {
  sadd(b, PICK(r, SURNAME_FRONT));
  sadd(b, PICK(r, SURNAME_BACK));
}

static void binomial(Rng *r, Str *b) {
  sadd(b, PICK(r, GENUS_ROOTS));
  sadd(b, PICK(r, GENUS_SUFFIXES));
  sadd(b, " ");
  sadd(b, PICK(r, SPECIES));
}

static void common_name(Rng *r, Str *b) {
  static const double w[] = {3, 2, 2, 2, 1};
  switch (rng_weighted(r, w, 5)) {
    case 0:
      sadd(b, PICK(r, QUALIFIERS)), sadd(b, " "), sadd(b, PICK(r, ADJECTIVES)), sadd(b, " "), sadd(b, PICK(r, FISH));
      break;
    case 1: sadd(b, PICK(r, QUALIFIERS)), sadd(b, " "), compound(r, b); break;
    case 2: sadd(b, PICK(r, ADJECTIVES)), sadd(b, " "), compound(r, b); break;
    case 3:
      sadd(b, PICK(r, COMPOUND_FRONT)), sadd(b, "-"), sadd(b, PICK(r, BODY_PARTS)), sadd(b, " "), sadd(b, PICK(r, FISH));
      break;
    default: sadd(b, PICK(r, ADJECTIVES)), sadd(b, " "), sadd(b, PICK(r, FISH)); break;
  }
}

static void character(Rng *r, Str *b) {
  static const double w[] = {3, 2, 2, 2, 2};
  switch (rng_weighted(r, w, 5)) {
    case 0: sadd(b, PICK(r, TITLES)), sadd(b, " "), sadd(b, PICK(r, FIRST_NAMES)), sadd(b, " "), surname(r, b); break;
    case 1: sadd(b, PICK(r, TITLES)), sadd(b, " "), surname(r, b); break;
    case 2: {
      const char *p = PICK(r, PARTICLES);
      size_t pl = strlen(p);
      const char *glue = (pl && p[pl - 1] == '\'') || !strcmp(p, "Mc") ? "" : " ";
      sadd(b, PICK(r, FIRST_NAMES)), sadd(b, " "), sadd(b, p), sadd(b, glue), surname(r, b);
      break;
    }
    case 3: sadd(b, PICK(r, FIRST_NAMES)), sadd(b, " the "), sadd(b, PICK(r, FISH)); break;
    default:
      sadd(b, PICK(r, FIRST_NAMES)), sadd(b, " the "), sadd(b, PICK(r, ADJECTIVES)), sadd(b, " "), sadd(b, PICK(r, FISH));
      break;
  }
}

static void generate_catch(const char *seed, ff_catch *out) {
  char buf[320];
  snprintf(buf, sizeof buf, "catch:%s", seed);
  Rng r = {hash_string(buf)};
  out->rare = rng_next(&r) < 0.04;
  Str name;
  if (out->rare) {
    name.n = 0;
    name.s[0] = 0;
    sadd(&name, PICK(&r, LEGENDS));
  } else {
    static const double w[] = {3, 4, 4};
    do {
      name.n = 0;
      name.s[0] = 0;
      int style = rng_weighted(&r, w, 3);
      if (style == 0) binomial(&r, &name);
      else if (style == 1) common_name(&r, &name);
      else character(&r, &name);
    } while (name.n > MAX_NAME_LENGTH);
  }
  // Names are at most MAX_NAME_LENGTH (and legends shorter), so this fits.
  size_t len = (size_t)name.n < sizeof out->name ? (size_t)name.n : sizeof out->name - 1;
  memcpy(out->name, name.s, len);
  out->name[len] = 0;
  int field = rng_index(&r, COUNT(NOTE_FIELDS));
  const char *value = NOTE_VALUES[field][rng_index(&r, NOTE_COUNTS[field])];
  snprintf(out->note, sizeof out->note, "%s: %s", NOTE_FIELDS[field], value);
}

// schedule.js
void ff_catch_for(const ff_time *t, const char *salt, ff_catch *out) {
  char key[32], seed[300];
  snprintf(key, sizeof key, "%d-%02d-%02dT%02d", t->year, t->month, t->day, t->hour);
  if (salt && *salt) snprintf(seed, sizeof seed, "%s/%s", salt, key);
  else snprintf(seed, sizeof seed, "%s", key);
  generate_catch(seed, out);
  snprintf(out->key, sizeof out->key, "%s", key);
}

// ---------------------------------------------------------------------------
// raster.js

typedef struct {
  int w, h;
  uint8_t *d;
} Bmp;

static void segment(Bmp *b, double x0, double y0, double x1, double y1, double r) {
  double fxa = js_max(0, floor(js_min(x0, x1) - r)), fxb = js_min(b->w - 1, ceil(js_max(x0, x1) + r));
  double fya = js_max(0, floor(js_min(y0, y1) - r)), fyb = js_min(b->h - 1, ceil(js_max(y0, y1) + r));
  if (isnan(fxa) || isnan(fxb) || isnan(fya) || isnan(fyb)) return; // the JS loops don't run
  if (fxa > fxb || fya > fyb) return;
  int xa = (int)fxa, xb = (int)fxb, ya = (int)fya, yb = (int)fyb;
  double dx = x1 - x0, dy = y1 - y0;
  double len2 = dx * dx + dy * dy;
  double r2 = r * r;
  for (int y = ya; y <= yb; y++) {
    double py = y + 0.5 - y0;
    for (int x = xa; x <= xb; x++) {
      double px = x + 0.5 - x0;
      double t = len2 ? (px * dx + py * dy) / len2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      double ex = px - t * dx, ey = py - t * dy;
      if (ex * ex + ey * ey <= r2) b->d[y * b->w + x] = 1;
    }
  }
}

// Bitmap.stroke for one polyline of n points (xy pairs).
static void stroke(Bmp *b, const double *xy, int n, double width) {
  double r = js_max(0.5, width / 2);
  if (n == 1) segment(b, xy[0], xy[1], xy[0], xy[1], r);
  for (int i = 1; i < n; i++) segment(b, xy[2 * i - 2], xy[2 * i - 1], xy[2 * i], xy[2 * i + 1], r);
}
static void line(Bmp *b, double x0, double y0, double x1, double y1, double width) {
  double xy[4] = {x0, y0, x1, y1};
  stroke(b, xy, 2, width);
}

// ---------------------------------------------------------------------------
// text.js

#define CAP_HEIGHT 21
#define BASELINE 9
enum { SIMPLEX, DUPLEX };

typedef struct {
  double advance;
  int nstrokes;
  int len[40];     // points per stroke
  double xy[400];  // all strokes' points, in order
} Glyph;

static void glyph(int font, uint32_t cp, Glyph *g) {
  g->nstrokes = 0;
  if (cp == 0xB0) { // the degree sign: a small circle at cap height
    g->advance = 9;
    g->nstrokes = 1;
    g->len[0] = 17;
    for (int i = 0; i < 17; i++) {
      double a = ((double)i / 16) * PI * 2;
      g->xy[2 * i] = 4 + 3 * fd_cos(a);
      g->xy[2 * i + 1] = -18 + 3 * fd_sin(a);
    }
    return;
  }
  const char *const *f = font == DUPLEX ? ROMAN_DUPLEX : ROMAN_SIMPLEX;
  const char *e = (cp >= 32 && cp <= 126) ? f[cp - 32] : f['?' - 32];
  int left = e[3] - 'R', right = e[4] - 'R';
  g->advance = right - left;
  int nxy = 0, cur = 0;
  for (int i = 5; e[i] && e[i + 1]; i += 2) {
    if (e[i] == ' ' && e[i + 1] == 'R') {
      if (cur > 1) g->len[g->nstrokes++] = cur; // strokes of one point are dropped
      else nxy -= cur * 2;
      cur = 0;
    } else {
      g->xy[nxy++] = e[i] - 'R' - left;
      g->xy[nxy++] = e[i + 1] - 'R' - BASELINE;
      cur++;
    }
  }
  if (cur > 1) g->len[g->nstrokes++] = cur;
}

static double measure(const char *s, int font, double size, double tracking) {
  double w = 0;
  int any = *s != 0;
  Glyph g;
  while (*s) {
    glyph(font, next_cp(&s), &g);
    w += g.advance + tracking;
  }
  return (w - (any ? tracking : 0)) * (size / CAP_HEIGHT);
}

static void draw_text(Bmp *b, const char *s, double x, double y, int font, double size, double italic, double tracking,
                      double width) {
  double k = size / CAP_HEIGHT, pen = 0, xy[400];
  Glyph g;
  while (*s) {
    glyph(font, next_cp(&s), &g);
    int off = 0;
    for (int st = 0; st < g.nstrokes; st++) {
      for (int i = 0; i < g.len[st]; i++) {
        double gx = g.xy[off + 2 * i], gy = g.xy[off + 2 * i + 1];
        xy[2 * i] = x + (pen + gx - italic * gy) * k;
        xy[2 * i + 1] = y + gy * k;
      }
      stroke(b, xy, g.len[st], width);
      off += 2 * g.len[st];
    }
    pen += g.advance + tracking;
  }
}

// ---------------------------------------------------------------------------
// weather.js: the icons

ff_sky ff_sky_for_code(int code) {
  if (code == 0) return FF_SUN;
  if (code == 1 || code == 2) return FF_PARTLY;
  if (code == 3) return FF_CLOUD;
  if (code == 45 || code == 48) return FF_FOG;
  if (code >= 51 && code <= 57) return FF_DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return FF_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return FF_SNOW;
  if (code >= 95 && code <= 99) return FF_STORM;
  return FF_CLOUD;
}

// Icons are built as polylines in a unit box, then placed and stroked. The
// icons are fixed shapes: the largest ("partly") needs 920 numbers in 11
// lines, the longest of them 254 points.
#define ICON_XY 1536
typedef struct {
  double xy[ICON_XY];
  int start[32], len[32];
  int nlines, nxy;
} Lines;

static void l_begin(Lines *l) {
  l->start[l->nlines] = l->nxy;
  l->len[l->nlines] = 0;
}
static void l_point(Lines *l, double x, double y) {
  l->xy[l->nxy++] = x;
  l->xy[l->nxy++] = y;
  l->len[l->nlines]++;
}
static void l_end(Lines *l) { l->nlines++; }
static void l_drop(Lines *l) { l->nxy = l->start[l->nlines]; }

static const double PUFFS[3][3] = {{0.3, 0.58, 0.17}, {0.53, 0.44, 0.23}, {0.77, 0.58, 0.16}};
#define BASE 0.74
static double half_chord(const double *p) { return sqrt(js_max(0, p[2] * p[2] - (BASE - p[1]) * (BASE - p[1]))); }
static double LEFT_, RIGHT_;
static void cloud_init(void) {
  LEFT_ = PUFFS[0][0] - half_chord(PUFFS[0]);
  RIGHT_ = PUFFS[2][0] + half_chord(PUFFS[2]);
}

static double cloud_top(double x) {
  double top = INFINITY;
  for (int i = 0; i < 3; i++) {
    double cx = PUFFS[i][0], cy = PUFFS[i][1], r = PUFFS[i][2];
    if (fabs(x - cx) <= r) top = js_min(top, cy - sqrt(js_max(0, r * r - (x - cx) * (x - cx))));
  }
  return x >= LEFT_ && x <= RIGHT_ ? top : INFINITY;
}

static int inside_cloud(double x, double y, double margin) {
  if (y > BASE + margin) return 0;
  for (int i = 0; i < 3; i++) {
    double cx = PUFFS[i][0], cy = PUFFS[i][1], r = PUFFS[i][2];
    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < (r + margin) * (r + margin)) return 1;
  }
  return x > LEFT_ - margin && x < RIGHT_ + margin && y > cloud_top(x) - margin;
}

static void cloud(Lines *l, double dx, double dy, double k) {
  for (int i = 0; i < 3; i++) {
    double cx = PUFFS[i][0], cy = PUFFS[i][1], r = PUFFS[i][2];
    int run = 0;
    l_begin(l);
    for (int s = 0; s <= 96; s++) {
      double a = ((double)s / 96) * TAU;
      double x = cx + r * fd_cos(a), y = cy + r * fd_sin(a);
      int hidden = y > BASE + 1e-6;
      for (int j = 0; j < 3 && !hidden; j++) {
        if (j == i) continue;
        double ox = PUFFS[j][0], oy = PUFFS[j][1], orr = PUFFS[j][2];
        if ((x - ox) * (x - ox) + (y - oy) * (y - oy) < (orr - 1e-3) * (orr - 1e-3)) hidden = 1;
      }
      if (!hidden && x > LEFT_ && x < RIGHT_ && y > cloud_top(x) + 0.01) hidden = 1;
      if (hidden) {
        if (run > 1) {
          l_end(l);
        } else {
          l_drop(l);
        }
        l_begin(l);
        run = 0;
      } else {
        l_point(l, dx + x * k, dy + y * k);
        run++;
      }
    }
    if (run > 1) l_end(l);
    else l_drop(l);
  }
  l_begin(l);
  l_point(l, dx + LEFT_ * k, dy + BASE * k);
  l_point(l, dx + RIGHT_ * k, dy + BASE * k);
  l_end(l);
}

static void sun(Lines *l, double cx, double cy, double r) {
  l_begin(l);
  for (int i = 0; i <= 32; i++) {
    double a = 0 + ((TAU - 0) * i) / 32;
    l_point(l, cx + r * fd_cos(a), cy + r * fd_sin(a));
  }
  l_end(l);
  for (int i = 0; i < 8; i++) {
    double a = ((double)i / 8) * TAU;
    l_begin(l);
    l_point(l, cx + r * 1.45 * fd_cos(a), cy + r * 1.45 * fd_sin(a));
    l_point(l, cx + r * 1.9 * fd_cos(a), cy + r * 1.9 * fd_sin(a));
    l_end(l);
  }
}

// Cut away the parts of src's lines hidden behind a cloud(dx, dy, k).
static void behind_cloud(Lines *out, const Lines *src, double dx, double dy, double k) {
  for (int li = 0; li < src->nlines; li++) {
    const double *pl = src->xy + src->start[li];
    int n = src->len[li], run = 0;
    l_begin(out);
    for (int i = 0; i < n; i++) {
      double x0 = pl[2 * i], y0 = pl[2 * i + 1];
      double x1 = i + 1 < n ? pl[2 * i + 2] : x0, y1 = i + 1 < n ? pl[2 * i + 3] : y0;
      int steps = i + 1 < n ? 12 : 1;
      for (int j = 0; j < steps; j++) {
        double px = x0 + ((x1 - x0) * j) / steps, py = y0 + ((y1 - y0) * j) / steps;
        if (inside_cloud((px - dx) / k, (py - dy) / k, 0.04)) {
          if (run > 1) l_end(out);
          else l_drop(out);
          l_begin(out);
          run = 0;
        } else {
          l_point(out, px, py);
          run++;
        }
      }
    }
    if (run > 1) l_end(out);
    else l_drop(out);
  }
}

static void slashes(Lines *l, double y0, double y1, double slant, const double *xs) {
  for (int i = 0; i < 3; i++) {
    l_begin(l);
    l_point(l, xs[i] + slant, y0);
    l_point(l, xs[i], y1);
    l_end(l);
  }
}

static void weather_icon(Bmp *b, ff_sky kind, double x, double y, double size, double width) {
  static Lines l, tmp;
  l.nlines = l.nxy = 0;
  switch (kind) {
    case FF_SUN: sun(&l, 0.5, 0.5, 0.2); break;
    case FF_PARTLY:
      tmp.nlines = tmp.nxy = 0;
      sun(&tmp, 0.36, 0.34, 0.15);
      behind_cloud(&l, &tmp, 0.1, 0.14, 0.9);
      cloud(&l, 0.1, 0.14, 0.9);
      break;
    case FF_FOG: {
      static const double f[4][4] = {{0.12, 0.38, 0.88, 0.38}, {0.2, 0.54, 0.8, 0.54}, {0.12, 0.7, 0.88, 0.7}, {0.25, 0.86, 0.75, 0.86}};
      for (int i = 0; i < 4; i++) {
        l_begin(&l);
        l_point(&l, f[i][0], f[i][1]);
        l_point(&l, f[i][2], f[i][3]);
        l_end(&l);
      }
      break;
    }
    case FF_DRIZZLE: {
      static const double xs[] = {0.32, 0.5, 0.68};
      cloud(&l, 0, -0.1, 1);
      slashes(&l, 0.76, 0.86, 0.03, xs);
      break;
    }
    case FF_RAIN: {
      static const double xs[] = {0.3, 0.5, 0.7};
      cloud(&l, 0, -0.1, 1);
      slashes(&l, 0.74, 0.98, 0.07, xs);
      break;
    }
    case FF_SNOW: {
      static const double xs[] = {0.32, 0.5, 0.68};
      cloud(&l, 0, -0.1, 1);
      for (int i = 0; i < 3; i++) {
        double cy = i == 1 ? 0.9 : 0.82, s = 0.06;
        for (int j = 0; j < 3; j++) {
          double a = ((double)j / 3) * PI + PI / 2;
          l_begin(&l);
          l_point(&l, xs[i] - s * fd_cos(a), cy - s * fd_sin(a));
          l_point(&l, xs[i] + s * fd_cos(a), cy + s * fd_sin(a));
          l_end(&l);
        }
      }
      break;
    }
    case FF_STORM:
      cloud(&l, 0, -0.1, 1);
      l_begin(&l);
      l_point(&l, 0.56, 0.66);
      l_point(&l, 0.44, 0.84);
      l_point(&l, 0.56, 0.84);
      l_point(&l, 0.46, 1.0);
      l_end(&l);
      break;
    default: cloud(&l, 0, 0.05, 1); break; // FF_CLOUD
  }
  static double xy[ICON_XY];
  for (int i = 0; i < l.nlines; i++) {
    const double *pl = l.xy + l.start[i];
    for (int j = 0; j < l.len[i]; j++) {
      xy[2 * j] = x + pl[2 * j] * size;
      xy[2 * j + 1] = y + pl[2 * j + 1] * size;
    }
    stroke(b, xy, l.len[i], width);
  }
}

// ---------------------------------------------------------------------------
// render.js

#define DESCENT 0.36
static const char *const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static double widest_date(void) {
  static double w = -1;
  if (w < 0) {
    w = 0;
    char t[32];
    for (int d = 0; d < 7; d++)
      for (int m = 0; m < 12; m++) {
        snprintf(t, sizeof t, "%s 30 %s", DAYS[d], MONTHS[m]);
        w = js_max(w, measure(t, DUPLEX, 1, 0));
      }
  }
  return w;
}

// The fish for the current hour, kept between minute updates.
static fd_drawing fish;
static char fish_name[sizeof ((ff_catch *)0)->name];
static int have_fish;
static double fish_bbox[4];

void ff_forget_fish(void) {
  if (have_fish) fishdraw_free(&fish);
  have_fish = 0;
}

static int fish_for(const char *name) {
  if (have_fish && !strcmp(fish_name, name)) return 0;
  ff_forget_fish();
  if (fishdraw(name, &fish)) return 1;
  have_fish = 1;
  snprintf(fish_name, sizeof fish_name, "%s", name);
  double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (int i = 0; i < fish.n; i++) {
    for (int j = 0; j < fish.lines[i].n; j++) {
      double x = fish.lines[i].points[j].x, y = fish.lines[i].points[j].y;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  fish_bbox[0] = x0;
  fish_bbox[1] = y0;
  fish_bbox[2] = x1 - x0;
  fish_bbox[3] = y1 - y0;
  return 0;
}

static Bmp rotate(const Bmp *b, int deg) {
  deg = ((deg % 360) + 360) % 360;
  Bmp o = {deg == 180 ? b->w : b->h, deg == 180 ? b->h : b->w, NULL};
  o.d = calloc((size_t)o.w * o.h, 1);
  if (!o.d) return o;
  for (int y = 0; y < b->h; y++) {
    for (int x = 0; x < b->w; x++) {
      if (!b->d[y * b->w + x]) continue;
      int nx, ny;
      if (deg == 90) nx = b->h - 1 - y, ny = x;
      else if (deg == 180) nx = b->w - 1 - x, ny = b->h - 1 - y;
      else nx = y, ny = b->w - 1 - x;
      o.d[ny * o.w + nx] = 1;
    }
  }
  return o;
}

int ff_render(const ff_time *t, const ff_options *opts, ff_bitmap *out, ff_catch *catch_out) {
  static int ready;
  if (!ready) {
    cloud_init();
    ready = 1;
  }
  out->pixels = NULL;
  int sideways = opts->rotate % 180 != 0;
  int W = sideways ? opts->height : opts->width, H = sideways ? opts->width : opts->height;
  Bmp bmp = {W, H, calloc((size_t)W * H, 1)};
  if (!bmp.d) return 1;
  ff_catch c;
  if (opts->catch_override) c = *opts->catch_override;
  else ff_catch_for(t, opts->salt, &c);
  if (catch_out) *catch_out = c;

  double u = js_min(H, W * 0.62);
  double m = js_max(4, js_round(js_min(W, H) * 0.045));
  int tiny = H < 200;
  double inner = W - 2 * m;

  // --- header
  char time[16], suffix[4] = "", mm[4];
  if (opts->hourly) snprintf(mm, sizeof mm, "00");
  else snprintf(mm, sizeof mm, "%02d", t->minute);
  if (opts->clock_12h) {
    snprintf(time, sizeof time, "%d:%s", t->hour % 12 ? t->hour % 12 : 12, mm);
    snprintf(suffix, sizeof suffix, "%s", t->hour < 12 ? "am" : "pm");
  } else {
    snprintf(time, sizeof time, "%02d:%s", t->hour, mm);
  }
  double gap = 1.2, icon_k = 1.5, icon_gap = 0.25, suffix_scale = 0.45;
  double suffix_w = *suffix ? suffix_scale * (0.3 + js_max(measure("am", SIMPLEX, 1, 0), measure("pm", SIMPLEX, 1, 0))) : 0;
  double time_and_date = measure(opts->clock_12h ? "12:00" : "00:00", DUPLEX, 1, 0) + suffix_w + gap + widest_date();
  char temps[48] = "";
  double weather_w = 0;
  if (opts->weather) {
    snprintf(temps, sizeof temps, "%.0f\xC2\xB0/%.0f\xC2\xB0", js_round(opts->weather->high) + 0.0,
             js_round(opts->weather->low) + 0.0);
    double temps_w = js_max(measure(temps, DUPLEX, 1, 0), measure("88\xC2\xB0/88\xC2\xB0", DUPLEX, 1, 0));
    weather_w = icon_k + icon_gap + temps_w + gap;
  }
  double head_size = 0.8 * js_min(0.75 * js_min(u * (tiny ? 0.2 : 0.13), inner / time_and_date), inner / (time_and_date + weather_w));
  double head_stroke = js_max(1.5, head_size / 14);
  double head_y = m + head_size;
  draw_text(&bmp, time, m, head_y, DUPLEX, head_size, 0, 0, head_stroke);
  double time_right = m + measure(time, DUPLEX, head_size, 0);
  if (*suffix) {
    double s = head_size * suffix_scale;
    draw_text(&bmp, suffix, time_right + s * 0.3, head_y, SIMPLEX, s, 0, 0, js_max(1, s / 10));
    time_right += s * 0.3 + measure(suffix, SIMPLEX, s, 0);
  }
  char date[32];
  snprintf(date, sizeof date, "%s %d %s", DAYS[t->weekday], t->day, MONTHS[t->month - 1]);
  double date_w = measure(date, DUPLEX, head_size, 0);
  draw_text(&bmp, date, W - m - date_w, head_y, DUPLEX, head_size, 0, 0, head_stroke);

  if (*temps) {
    double icon_size = head_size * icon_k;
    double w = icon_size + head_size * icon_gap + measure(temps, DUPLEX, head_size, 0);
    double x = (time_right + W - m - date_w - w) / 2;
    weather_icon(&bmp, opts->weather->kind, x, head_y - head_size / 2 - icon_size / 2, icon_size, js_max(1, head_size / 13));
    draw_text(&bmp, temps, x + icon_size + head_size * icon_gap, head_y, DUPLEX, head_size, 0, 0, head_stroke);
  }

  // --- rule and rare badge
  double rule_y = head_y + head_size * DESCENT + m * 0.3;
  line(&bmp, m, rule_y, W - m, rule_y, js_max(1, u / 300));
  double box_top = rule_y + m * 0.45;
  if (c.rare && !tiny) {
    double s = js_max(7, u * 0.034);
    const char *label = "RARE CATCH!";
    double w = measure(label, SIMPLEX, s, 3);
    double pad = s * 0.5;
    double x0 = W - m - w - 2 * pad;
    double y1 = rule_y + s + 2 * pad;
    draw_text(&bmp, label, x0 + pad, y1 - pad, SIMPLEX, s, 0, 3, js_max(1, s / 10));
    double box[8] = {x0, rule_y, x0, y1, W - m, y1, W - m, rule_y};
    stroke(&bmp, box, 4, js_max(1, s / 10));
  }

  // --- footer
  double bottom = H - m;
  int have_name_baseline = 0;
  double name_baseline = 0;
  double note_size = js_max(7, u * 0.029);
  note_size = js_min(note_size, note_size * inner / measure(c.note, SIMPLEX, note_size, 0));
  if (!tiny && note_size >= 7) {
    double w = measure(c.note, SIMPLEX, note_size, 0);
    double y = bottom - note_size * DESCENT;
    draw_text(&bmp, c.note, (W - w) / 2, y, SIMPLEX, note_size, 0, 0, js_max(1, note_size / 12));
    name_baseline = y - note_size * 1.9;
    have_name_baseline = 1;
  }
  double italic = 0.3;
  double name_size = u * (tiny ? 0.075 : 0.05);
  name_size = js_min(name_size, inner / (measure(c.name, SIMPLEX, 1, 0) + italic));
  double name_w = measure(c.name, SIMPLEX, name_size, 0);
  bottom = have_name_baseline ? name_baseline : bottom - name_size * DESCENT;
  draw_text(&bmp, c.name, (W - name_w) / 2 - (italic * name_size) / 2, bottom, SIMPLEX, name_size, italic, 0,
            js_max(1, name_size / 11));

  // --- the fish
  double box_bottom = bottom - name_size * 1.05 - m * 0.45;
  if (fish_for(c.name)) {
    free(bmp.d);
    return 1;
  }
  double scale = js_min(inner / fish_bbox[2], (box_bottom - box_top) / fish_bbox[3]);
  double ox = m + (inner - fish_bbox[2] * scale) / 2 - fish_bbox[0] * scale;
  double oy = box_top + (box_bottom - box_top - fish_bbox[3] * scale) / 2 - fish_bbox[1] * scale;
  double width = js_max(1, scale * 0.9);
  int cap = 0;
  double *xy = NULL;
  for (int i = 0; i < fish.n; i++) {
    int n = fish.lines[i].n;
    if (n * 2 > cap) {
      cap = n * 2;
      double *nxy = realloc(xy, sizeof(double) * cap);
      if (!nxy) {
        free(xy);
        free(bmp.d);
        return 1;
      }
      xy = nxy;
    }
    for (int j = 0; j < n; j++) {
      xy[2 * j] = fish.lines[i].points[j].x * scale + ox;
      xy[2 * j + 1] = fish.lines[i].points[j].y * scale + oy;
    }
    stroke(&bmp, xy, n, width);
  }
  free(xy);

  if (((opts->rotate % 360) + 360) % 360) {
    Bmp r = rotate(&bmp, opts->rotate);
    free(bmp.d);
    if (!r.d) return 1;
    bmp = r;
  }
  out->width = bmp.w;
  out->height = bmp.h;
  out->pixels = bmp.d;
  return 0;
}

void ff_bitmap_free(ff_bitmap *b) {
  free(b->pixels);
  b->pixels = NULL;
}
