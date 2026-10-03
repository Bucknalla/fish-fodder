// fishdraw in C.
//
// A function-for-function port of src/vendor/fishdraw.js (fishdraw by
// Lingdong Huang, https://github.com/LingDong-/fishdraw, MIT licence), made
// to produce bit-identical output. Function names match the JavaScript, so
// the two read side by side. What it takes to match exactly:
//
//  * Maths comes from fdlibm.c, the same fdlibm the JavaScript bundles.
//  * Points are shared by reference wherever the JavaScript shares arrays,
//    because fishdraw edits some points in place and the edit must show up
//    everywhere the point is used.
//  * JavaScript number semantics: ToInt32 for ~~, |0 and shifts; Math.min
//    and Math.max propagate NaN; slice() takes negative indices; `y || 0`.
//  * Sorts are stable, as V8's is.
//  * C leaves the order of argument evaluation unspecified, so every call
//    that draws random numbers (rand, noise's first call, and anything that
//    calls them) gets its own statement, in the JavaScript's order.
//  * Compile with -ffp-contract=off so no multiply-add is fused.
//
// Memory comes from one arena per drawing, released all at once.

#include "fishdraw.h"

#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fdlibm.h"
#ifdef FISHDRAW_TRACE
#include <stdio.h>
#define TRACE(label) fprintf(stderr, "%-14s main %5.1f MB  peak %5.1f MB\n", label, arena_size[A_MAIN] / 1048576.0, peak_bytes / 1048576.0)
#else
#define TRACE(label)
#endif

#define PI 3.141592653589793
#define MATH_E 2.718281828459045

// ---------------------------------------------------------------------------
// Errors: places where the JavaScript would throw end the drawing.

static jmp_buf fail_jmp;
static void fail(void) { longjmp(fail_jmp, 1); }

// ---------------------------------------------------------------------------
// Arena

static void *(*mem_alloc)(size_t) = malloc;
static void (*mem_free)(void *) = free;

void fishdraw_set_allocator(void *(*alloc)(size_t), void (*release)(void *)) {
  mem_alloc = alloc ? alloc : malloc;
  mem_free = release ? release : free;
}

typedef struct Chunk {
  struct Chunk *next;
  size_t cap, used;
  max_align_t data[];
} Chunk;

// Three arenas:
//   MAIN     what the drawing is made of; released when the drawing is done.
//   STAGE    a stage's working data whose results get copied out (the scale
//            mesh, for one); reset when the stage ends.
//   SCRATCH  data that only lives inside one call (intersection records,
//            poly_union's tables, temporary copies); every call takes a mark
//            and resets to it before returning.
// Lists remember their arena, so they always grow in the right one. New
// lists and points go to list_arena / point_arena, which are MAIN except
// while building something temporary.
enum { A_MAIN, A_STAGE, A_SCRATCH, A_COUNT };
static Chunk *arenas[A_COUNT];
static size_t arena_size[A_COUNT], peak_bytes;
static int list_arena = A_MAIN, point_arena = A_MAIN;

static void *alloc_in(int a, size_t n) {
  n = (n + 15) & ~(size_t)15;
  Chunk *c = arenas[a];
  if (!c || c->used + n > c->cap) {
    size_t cap = n > (1 << 16) ? n : (1 << 16);
    c = mem_alloc(sizeof(Chunk) + cap);
    if (!c) fail();
    c->next = arenas[a];
    c->cap = cap;
    c->used = 0;
    arenas[a] = c;
    arena_size[a] += sizeof(Chunk) + cap;
    size_t total = 0;
    for (int i = 0; i < A_COUNT; i++) total += arena_size[i];
    if (total > peak_bytes) peak_bytes = total;
  }
  void *p = (char *)c->data + c->used;
  c->used += n;
  return p;
}
static void *amalloc(size_t n) { return alloc_in(A_MAIN, n); }
static void *smalloc(size_t n) { return alloc_in(A_SCRATCH, n); }

typedef struct {
  int a;
  Chunk *c;
  size_t used;
} Mark;
static Mark mark_of(int a) {
  Mark m = {a, arenas[a], arenas[a] ? arenas[a]->used : 0};
  return m;
}
static Mark scratch_mark(void) { return mark_of(A_SCRATCH); }
static void reset_to(Mark m) {
  while (arenas[m.a] != m.c) {
    Chunk *next = arenas[m.a]->next;
    arena_size[m.a] -= sizeof(Chunk) + arenas[m.a]->cap;
    mem_free(arenas[m.a]);
    arenas[m.a] = next;
  }
  if (arenas[m.a]) arenas[m.a]->used = m.used;
}
static void scratch_reset(Mark m) { reset_to(m); }

// Lists kept on the heap so they can be freed early (the growing clipper of
// the scale mesh); tracked so a failure can still free them.
static void *heap_lists[4];

static void arena_release(void) {
  for (int a = 0; a < A_COUNT; a++) {
    while (arenas[a]) {
      Chunk *next = arenas[a]->next;
      mem_free(arenas[a]);
      arenas[a] = next;
    }
    arena_size[a] = 0;
  }
  for (int i = 0; i < 4; i++) {
    mem_free(heap_lists[i]);
    heap_lists[i] = NULL;
  }
  list_arena = point_arena = A_MAIN;
}

// ---------------------------------------------------------------------------
// JavaScript number semantics

// ECMAScript ToInt32: what ~~x, x|0 and the shift operators do to a number.
static int32_t toi32(double d) {
  if (!isfinite(d)) return 0;
  d = trunc(d);
  d = fmod(d, 4294967296.0);
  if (d < 0) d += 4294967296.0;
  return (int32_t)(uint32_t)d;
}
static int32_t shl(int32_t v, int n) { return (int32_t)((uint32_t)v << n); }
static int32_t sar(int32_t v, int n) { return v < 0 ? ~(int32_t)(~(uint32_t)v >> n) : v >> n; }

// Math.min / Math.max: NaN wins; -0 is less than +0.
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
static double js_sign(double a) { return a > 0 ? 1 : a < 0 ? -1 : a; }

// ---------------------------------------------------------------------------
// Points and arrays. P is a JS [x, y] array; L an array of points; LL an
// array of polylines. Both arrays hold pointers, so sharing matches the JS.

typedef struct { double x, y; } P;
typedef struct { P **a; int n, cap, ar; } L;
typedef struct { L **a; int n, cap, ar; } LL;

static P *P_new(double x, double y) {
  P *p = alloc_in(point_arena, sizeof(P));
  p->x = x;
  p->y = y;
  return p;
}
static P *P_copy(const P *p) { return P_new(p->x, p->y); }

static L *L_new(int cap) {
  L *l = alloc_in(list_arena, sizeof(L));
  l->ar = list_arena;
  l->cap = cap < 4 ? 4 : cap;
  l->n = 0;
  l->a = alloc_in(l->ar, sizeof(P *) * l->cap);
  return l;
}
static void L_push(L *l, P *p) {
  if (l->n == l->cap) {
    P **a = alloc_in(l->ar, sizeof(P *) * l->cap * 2);
    memcpy(a, l->a, sizeof(P *) * l->n);
    l->a = a;
    l->cap *= 2;
  }
  l->a[l->n++] = p;
}
static void L_unshift(L *l, P *p) {
  L_push(l, p);
  memmove(l->a + 1, l->a, sizeof(P *) * (l->n - 1));
  l->a[0] = p;
}
// Array.prototype.slice(start, end) with JS index rules.
static int js_index(double i, int n) {
  if (i < 0) return i + n < 0 ? 0 : (int)(i + n);
  return i > n ? n : (int)i;
}
static L *L_slice(const L *l, double start, double end) {
  int s = js_index(start, l->n), e = js_index(end, l->n);
  L *o = L_new(e - s);
  for (int i = s; i < e; i++) L_push(o, l->a[i]);
  return o;
}
static L *L_copy(const L *l) { return L_slice(l, 0, l->n); }
static L *L_reverse(L *l) { // in place, like JS; returns the same array
  for (int i = 0, j = l->n - 1; i < j; i++, j--) {
    P *t = l->a[i];
    l->a[i] = l->a[j];
    l->a[j] = t;
  }
  return l;
}
static L *L_concat(const L *a, const L *b) {
  L *o = L_new(a->n + b->n);
  for (int i = 0; i < a->n; i++) L_push(o, a->a[i]);
  for (int i = 0; i < b->n; i++) L_push(o, b->a[i]);
  return o;
}
static L *L_of(int n, ...);

static LL *LL_new(int cap) {
  LL *l = alloc_in(list_arena, sizeof(LL));
  l->ar = list_arena;
  l->cap = cap < 4 ? 4 : cap;
  l->n = 0;
  l->a = alloc_in(l->ar, sizeof(L *) * l->cap);
  return l;
}
static void LL_push(LL *l, L *p) {
  if (l->n == l->cap) {
    L **a = alloc_in(l->ar, sizeof(L *) * l->cap * 2);
    memcpy(a, l->a, sizeof(L *) * l->n);
    l->a = a;
    l->cap *= 2;
  }
  l->a[l->n++] = p;
}
static void LL_push_all(LL *l, const LL *o) {
  for (int i = 0; i < o->n; i++) LL_push(l, o->a[i]);
}
static LL *LL_concat(const LL *a, const LL *b) {
  LL *o = LL_new(a->n + b->n);
  LL_push_all(o, a);
  LL_push_all(o, b);
  return o;
}
static LL *LL_one(L *l) {
  LL *o = LL_new(1);
  LL_push(o, l);
  return o;
}

#include <stdarg.h>
static L *L_of(int n, ...) {
  va_list ap;
  va_start(ap, n);
  L *l = L_new(n);
  for (int i = 0; i < n; i++) L_push(l, va_arg(ap, P *));
  va_end(ap);
  return l;
}

// ---------------------------------------------------------------------------
// Random numbers and noise

static int32_t jsr;

static double rnd(void) {
  jsr ^= shl(jsr, 17);
  jsr ^= sar(jsr, 13);
  jsr ^= shl(jsr, 5);
  return (double)(uint32_t)jsr / 4294967295.0;
}

#define PERLIN_YWRAPB 4
#define PERLIN_YWRAP (1 << PERLIN_YWRAPB)
#define PERLIN_ZWRAPB 8
#define PERLIN_ZWRAP (1 << PERLIN_ZWRAPB)
#define PERLIN_SIZE 4095
static double perlin[PERLIN_SIZE + 1];
static int perlin_ready;

static double scaled_cosine(double i) { return 0.5 * (1.0 - fd_cos(i * PI)); }

static double noise(double x, double y, double z) {
  if (y == 0 || isnan(y)) y = 0; // y = y || 0: NaN and -0 become +0
  if (z == 0 || isnan(z)) z = 0;
  if (!perlin_ready) {
    for (int i = 0; i < PERLIN_SIZE + 1; i++) perlin[i] = rnd();
    perlin_ready = 1;
  }
  if (x < 0) x = -x;
  if (y < 0) y = -y;
  if (z < 0) z = -z;
  double xi = floor(x), yi = floor(y), zi = floor(z);
  double xf = x - xi, yf = y - yi, zf = z - zi;
  double rxf, ryf, r = 0, ampl = 0.5, n1, n2, n3;
  for (int o = 0; o < 4; o++) {
    double of = xi + shl(toi32(yi), PERLIN_YWRAPB) + shl(toi32(zi), PERLIN_ZWRAPB);
    rxf = scaled_cosine(xf);
    ryf = scaled_cosine(yf);
    n1 = perlin[toi32(of) & PERLIN_SIZE];
    n1 += rxf * (perlin[toi32(of + 1) & PERLIN_SIZE] - n1);
    n2 = perlin[toi32(of + PERLIN_YWRAP) & PERLIN_SIZE];
    n2 += rxf * (perlin[toi32(of + PERLIN_YWRAP + 1) & PERLIN_SIZE] - n2);
    n1 += ryf * (n2 - n1);
    of += PERLIN_ZWRAP;
    n2 = perlin[toi32(of) & PERLIN_SIZE];
    n2 += rxf * (perlin[toi32(of + 1) & PERLIN_SIZE] - n2);
    n3 = perlin[toi32(of + PERLIN_YWRAP) & PERLIN_SIZE];
    n3 += rxf * (perlin[toi32(of + PERLIN_YWRAP + 1) & PERLIN_SIZE] - n3);
    n2 += ryf * (n3 - n2);
    n1 += scaled_cosine(zf) * (n2 - n1);
    r += n1 * ampl;
    ampl *= 0.5;
    xi = shl(toi32(xi), 1);
    xf *= 2;
    yi = shl(toi32(yi), 1);
    yf *= 2;
    zi = shl(toi32(zi), 1);
    zf *= 2;
    if (xf >= 1.0) { xi++; xf--; }
    if (yf >= 1.0) { yi++; yf--; }
    if (zf >= 1.0) { zi++; zf--; }
  }
  return r;
}
static double noise1(double x) { return noise(x, 0, 0); }
static double noise2(double x, double y) { return noise(x, y, 0); }

// ---------------------------------------------------------------------------
// Geometry basics

static double dist(double x0, double y0, double x1, double y1) { return fd_hypot(x1 - x0, y1 - y0); }
static double distp(const P *a, const P *b) { return dist(a->x, a->y, b->x, b->y); }
static double lerp(double a, double b, double t) { return a * (1 - t) + b * t; }
static P *lerp2d(double x0, double y0, double x1, double y1, double t) {
  return P_new(x0 * (1 - t) + x1 * t, y0 * (1 - t) + y1 * t);
}
static P *lerp2dp(const P *a, const P *b, double t) { return lerp2d(a->x, a->y, b->x, b->y, t); }

typedef struct { double x, y, w, h; } BBox;
static BBox get_bbox(const L *points) {
  double xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
  for (int i = 0; i < points->n; i++) {
    double x = points->a[i]->x, y = points->a[i]->y;
    xmin = js_min(xmin, x);
    ymin = js_min(ymin, y);
    xmax = js_max(xmax, x);
    ymax = js_max(ymax, y);
  }
  BBox b = {xmin, ymin, xmax - xmin, ymax - ymin};
  return b;
}
static L *LL_flat(const LL *l) {
  int n = 0;
  for (int i = 0; i < l->n; i++) n += l->a[i]->n;
  L *o = L_new(n);
  for (int i = 0; i < l->n; i++)
    for (int j = 0; j < l->a[i]->n; j++) L_push(o, l->a[i]->a[j]);
  return o;
}

static double pt_in_pl(double x, double y, double x0, double y0, double x1, double y1) {
  double dx = x1 - x0, dy = y1 - y0;
  return (x - x0) * dy - (y - y0) * dx;
}

typedef struct Isect {
  double t, s;
  int side;
  P *xy;
  int other, jump;
  int mir_idx, mir_k, mir_z; // isect_mir[...] in the JS
} Isect;

static Isect *seg_isect(double p0x, double p0y, double p1x, double p1y, double q0x, double q0y, double q1x,
                        double q1y, int is_ray) {
  double d0x = p1x - p0x, d0y = p1y - p0y, d1x = q1x - q0x, d1y = q1y - q0y;
  double vc = d0x * d1y - d0y * d1x;
  if (vc == 0) return NULL;
  double vcn = vc * vc;
  double q0x_p0x = q0x - p0x, q0y_p0y = q0y - p0y;
  double vc_vcn = vc / vcn;
  double t = (q0x_p0x * d1y - q0y_p0y * d1x) * vc_vcn;
  double s = (q0x_p0x * d0y - q0y_p0y * d0x) * vc_vcn;
  if (0 <= t && (is_ray || t < 1) && 0 <= s && s < 1) {
    Isect *r = smalloc(sizeof(Isect));
    memset(r, 0, sizeof(Isect));
    r->t = t;
    r->s = s;
    r->xy = P_new(p1x * t + p0x * (1 - t), p1y * t + p0y * (1 - t));
    r->side = pt_in_pl(p0x, p0y, p1x, p1y, q0x, q0y) < 0 ? 1 : -1;
    return r;
  }
  return NULL;
}

// A growable array of intersections, sorted stably by t like V8's sort.
typedef struct { Isect **a; int n, cap; } IL;
static void IL_push(IL *l, Isect *x) {
  if (l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 4;
    Isect **a = smalloc(sizeof(Isect *) * cap);
    if (l->n) memcpy(a, l->a, sizeof(Isect *) * l->n);
    l->a = a;
    l->cap = cap;
  }
  l->a[l->n++] = x;
}
static void IL_sort(IL *l) { // insertion sort on (a.t - b.t): stable
  for (int i = 1; i < l->n; i++) {
    Isect *x = l->a[i];
    int j = i - 1;
    while (j >= 0 && l->a[j]->t - x->t > 0) {
      l->a[j + 1] = l->a[j];
      j--;
    }
    l->a[j + 1] = x;
  }
}

static L *poly_bridge(const L *poly0, const L *poly1) {
  double dmin = INFINITY;
  int i0 = -1, i1 = -1;
  for (int i = 0; i < poly0->n; i++) {
    for (int j = 0; j < poly1->n; j++) {
      double dx = poly0->a[i]->x - poly1->a[j]->x;
      double dy = poly0->a[i]->y - poly1->a[j]->y;
      double d2 = dx * dx + dy * dy;
      if (d2 < dmin) {
        dmin = d2;
        i0 = i;
        i1 = j;
      }
    }
  }
  if (i0 < 0) fail(); // imin[0] of null
  L *u = L_slice(poly0, 0, i0);
  u = L_concat(u, L_slice(poly1, i1, poly1->n));
  u = L_concat(u, L_slice(poly1, 0, i1));
  u = L_concat(u, L_slice(poly0, i0, poly0->n));
  return u;
}

// One vertex of poly_union: its intersection list and isects_map, which is
// keyed by "poly,segment".
typedef struct { int key_idx, key_i; Isect *x; } MapEnt;
typedef struct {
  P *xy;
  IL isects;
  MapEnt *map;
  int nmap, capmap;
} Vert;

static Isect *vert_map_get(const Vert *v, int idx, int i) {
  for (int k = 0; k < v->nmap; k++)
    if (v->map[k].key_idx == idx && v->map[k].key_i == i) return v->map[k].x;
  return NULL;
}
static void vert_map_set(Vert *v, int idx, int i, Isect *x) {
  for (int k = 0; k < v->nmap; k++)
    if (v->map[k].key_idx == idx && v->map[k].key_i == i) {
      v->map[k].x = x;
      return;
    }
  if (v->nmap == v->capmap) {
    int cap = v->capmap ? v->capmap * 2 : 4;
    MapEnt *m = smalloc(sizeof(MapEnt) * cap);
    if (v->nmap) memcpy(m, v->map, sizeof(MapEnt) * v->nmap);
    v->map = m;
    v->capmap = cap;
  }
  v->map[v->nmap].key_idx = idx;
  v->map[v->nmap].key_i = i;
  v->map[v->nmap].x = x;
  v->nmap++;
}

// The self_isect branch of the JS is unreachable from fishdraw (it is only
// taken by a recursive call that the first call never makes), so it is left
// out here.
static L *poly_union_inner(const L *poly0, const L *poly1) {
  int n0 = poly0->n, n1 = poly1->n;
  Vert *verts0 = smalloc(sizeof(Vert) * (n0 ? n0 : 1));
  Vert *verts1 = smalloc(sizeof(Vert) * (n1 ? n1 : 1));
  memset(verts0, 0, sizeof(Vert) * (n0 ? n0 : 1));
  memset(verts1, 0, sizeof(Vert) * (n1 ? n1 : 1));
  for (int i = 0; i < n0; i++) verts0[i].xy = poly0->a[i];
  for (int i = 0; i < n1; i++) verts1[i].xy = poly1->a[i];

  int has_isect = 0;
  for (int pass = 0; pass < 2; pass++) {
    const L *poly = pass ? poly1 : poly0, *other = pass ? poly0 : poly1;
    Vert *out = pass ? verts1 : verts0, *oout = pass ? verts0 : verts1;
    int idx = pass, n = poly->n, m = other->n;
    for (int i = 0; i < n; i++) {
      Vert *p = &out[i];
      int i1 = (i + 1 + n) % n;
      const P *a = poly->a[i], *b = poly->a[i1];
      for (int j = 0; j < m; j++) {
        int j1 = (j + 1 + m) % m;
        const P *c = other->a[j], *d = other->a[j1];
        Isect *xx;
        Isect *ox = vert_map_get(&oout[j], idx, i);
        if (ox) {
          xx = smalloc(sizeof(Isect));
          memset(xx, 0, sizeof(Isect));
          xx->t = ox->s;
          xx->s = ox->t;
          xx->xy = ox->xy;
          xx->side = pt_in_pl(a->x, a->y, b->x, b->y, c->x, c->y) < 0 ? 1 : -1;
        } else {
          xx = seg_isect(a->x, a->y, b->x, b->y, c->x, c->y, d->x, d->y, 0);
        }
        if (xx) {
          has_isect = 1;
          xx->other = j;
          xx->jump = 1;
          IL_push(&p->isects, xx);
          vert_map_set(p, 1 - idx, j, xx);
        }
      }
      IL_sort(&p->isects);
    }
  }

  if (!has_isect) return poly_bridge(poly0, poly1);

  // mirror_isects: where each intersection continues on the other polygon.
  for (int pass = 0; pass < 2; pass++) {
    Vert *v0 = pass ? verts1 : verts0, *v1 = pass ? verts0 : verts1;
    int n = pass ? n1 : n0, idx = pass;
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < v0[i].isects.n; j++) {
        Isect *x = v0[i].isects.a[j];
        int jump = x->jump;
        int jd = jump ? (1 - idx) : idx;
        int k = x->other;
        Vert *target = &(jump ? v1 : v0)[k];
        int z = -1;
        for (int q = 0; q < target->isects.n; q++) {
          if (target->isects.a[q]->jump == jump && target->isects.a[q]->other == i) {
            z = q;
            break;
          }
        }
        x->mir_idx = jd;
        x->mir_k = k;
        x->mir_z = z;
      }
    }
  }

  // The leftmost vertex is on the outline; start there.
  double xmin = INFINITY;
  int a0 = -1, a1 = -1;
  for (int i = 0; i < n0; i++)
    if (poly0->a[i]->x < xmin) {
      xmin = poly0->a[i]->x;
      a0 = 0;
      a1 = i;
    }
  for (int i = 0; i < n1; i++)
    if (poly1->a[i]->x < xmin) {
      xmin = poly1->a[i]->x;
      a0 = 1;
      a1 = i;
    }
  if (a0 < 0) fail(); // amin[0] of null

  // check_concavity
  const L *cpoly = a0 ? poly1 : poly0;
  int cn = cpoly->n;
  const P *ca = cpoly->a[(a1 - 1 + cn) % cn], *cb = cpoly->a[a1], *cc = cpoly->a[(a1 + 1) % cn];
  int cw = pt_in_pl(ca->x, ca->y, cb->x, cb->y, cc->x, cc->y) < 0 ? 1 : -1;

  // trace_outline(a0, a1, -1, cw): the JS recursion, as a loop.
  L *out = L_new(16);
  int idx = a0, i0 = a1, j0 = -1, dir = cw;
  int z0 = idx, z1 = i0, z2 = j0, first = 1;
  long steps = 0;
  for (;;) {
    if (first) {
      first = 0;
    } else if (idx == z0 && i0 == z1 && j0 == z2) {
      break;
    }
    // V8 runs out of stack long before this; fishdraw would throw.
    if (++steps > 2000000) fail();
    Vert *verts = idx ? verts1 : verts0;
    int n = idx ? n1 : n0;
    Vert *p = &verts[i0];
    int i1 = (i0 + dir + n) % n;
    if (j0 == -1) {
      L_push(out, p->xy);
      if (dir < 0) {
        i0 = i1;
        j0 = verts[i1].isects.n - 1;
      } else if (!verts[i0].isects.n) {
        i0 = i1;
        j0 = -1;
      } else {
        j0 = 0;
      }
    } else if (j0 >= p->isects.n) {
      i0 = i1;
      j0 = -1;
    } else {
      if (j0 < 0) fail(); // p.isects[j0] is undefined in the JS
      Isect *q = p->isects.a[j0];
      L_push(out, q->xy);
      if (q->side * dir < 0) {
        idx = q->mir_idx;
        i0 = q->mir_k;
        j0 = q->mir_z - 1;
        dir = -1;
      } else {
        idx = q->mir_idx;
        i0 = q->mir_k;
        j0 = q->mir_z + 1;
        dir = 1;
      }
    }
  }
  if (out->n < 3) return L_new(0);
  return out;
}

static L *poly_union(const L *poly0, const L *poly1) {
  Mark m = scratch_mark();
  L *out = poly_union_inner(poly0, poly1);
  scratch_reset(m);
  return out;
}

// poly_union for a clipper that's replaced again and again (the scale mesh):
// the result lives on the heap in heap_lists[0], freeing the one before.
static L *poly_union_heap(const L *poly0, const L *poly1) {
  Mark m = scratch_mark();
  int la = list_arena;
  list_arena = A_SCRATCH;
  L *u = poly_union_inner(poly0, poly1);
  list_arena = la;
  L *h = mem_alloc(sizeof(L) + sizeof(P *) * (u->n ? u->n : 1));
  if (!h) fail();
  h->a = (P **)(h + 1);
  h->n = h->cap = u->n;
  h->ar = -1; // never grows
  memcpy(h->a, u->a, sizeof(P *) * u->n);
  mem_free(heap_lists[0]); // poly0 may be the old one; it's no longer needed
  heap_lists[0] = h;
  scratch_reset(m);
  return h;
}

// seg_isect_poly(..., is_ray = true).length, without building the records.
static int ray_crossings(double p0x, double p0y, double p1x, double p1y, const L *poly) {
  int n = poly->n, count = 0;
  for (int i = 0; i < n; i++) {
    const P *a = poly->a[i], *b = poly->a[(i + 1) % n];
    double d0x = p1x - p0x, d0y = p1y - p0y, d1x = b->x - a->x, d1y = b->y - a->y;
    double vc = d0x * d1y - d0y * d1x;
    if (vc == 0) continue;
    double vcn = vc * vc;
    double q0x_p0x = a->x - p0x, q0y_p0y = a->y - p0y;
    double vc_vcn = vc / vcn;
    double t = (q0x_p0x * d1y - q0y_p0y * d1x) * vc_vcn;
    double s = (q0x_p0x * d0y - q0y_p0y * d0x) * vc_vcn;
    if (0 <= t && 0 <= s && s < 1) count++;
  }
  return count;
}

static IL seg_isect_poly(double x0, double y0, double x1, double y1, const L *poly, int is_ray) {
  IL isects = {0};
  int n = poly->n;
  for (int i = 0; i < n; i++) {
    const P *a = poly->a[i], *b = poly->a[(i + 1) % n];
    Isect *xx = seg_isect(x0, y0, x1, y1, a->x, a->y, b->x, b->y, is_ray);
    if (xx) IL_push(&isects, xx);
  }
  IL_sort(&isects);
  return isects;
}

// clip()/binclip() results: out.true and out.false.
typedef struct { LL *t, *f; } Split;

// Polylines are only created once a point goes in, which gives the same
// lists as the JS's [[]] start followed by filter(x => x.length).
typedef struct {
  LL *list[2]; // [false, true]
  L *cur[2];
} Sides;

static void side_push(Sides *s, int io, P *p) {
  if (!s->list[io]) return; // that side isn't wanted
  if (!s->cur[io]) {
    s->cur[io] = L_new(4);
    LL_push(s->list[io], s->cur[io]);
  }
  L_push(s->cur[io], p);
}
static void side_start(Sides *s, int io, P *p) {
  if (!s->list[io]) return;
  s->cur[io] = L_new(4);
  LL_push(s->list[io], s->cur[io]);
  L_push(s->cur[io], p);
}

static void clip_into(const L *polyline, const L *polygon, LL *out_t, LL *out_f) {
  if (!polyline->n) return;
  Mark m = scratch_mark();
  const P *p0 = polyline->a[0];
  int zero = ray_crossings(p0->x, p0->y, p0->x + MATH_E, p0->y + PI, polygon) % 2 != 0;
  Sides out = {{out_f, out_t}, {NULL, NULL}};
  int io = zero;
  for (int i = 0; i < polyline->n; i++) {
    P *a = polyline->a[i];
    side_push(&out, io, a);
    if (i + 1 >= polyline->n) break;
    P *b = polyline->a[i + 1];
    IL isects = seg_isect_poly(a->x, a->y, b->x, b->y, polygon, 0);
    for (int j = 0; j < isects.n; j++) {
      side_push(&out, io, isects.a[j]->xy);
      io = !io;
      side_start(&out, io, isects.a[j]->xy);
    }
  }
  scratch_reset(m);
}

static Split clip_multi(const LL *polylines, const L *polygon) {
  Split out = {LL_new(polylines->n), LL_new(polylines->n)};
  for (int i = 0; i < polylines->n; i++) clip_into(polylines->a[i], polygon, out.t, out.f);
  return out;
}
// clip_multi(polylines, polygon).true / .false, building only that side.
static LL *clip_in(const LL *polylines, const L *polygon) {
  LL *out = LL_new(polylines->n);
  for (int i = 0; i < polylines->n; i++) clip_into(polylines->a[i], polygon, out, NULL);
  return out;
}
static LL *clip_out(const LL *polylines, const L *polygon) {
  LL *out = LL_new(polylines->n);
  for (int i = 0; i < polylines->n; i++) clip_into(polylines->a[i], polygon, NULL, out);
  return out;
}
static LL *clip1_out(const L *polyline, const L *polygon) {
  LL *out = LL_new(4);
  clip_into(polyline, polygon, NULL, out);
  return out;
}

// A predicate (x, y, t) -> bool for binclip.
typedef struct {
  int (*f)(void *ctx, double x, double y, double t);
  void *ctx;
} Pred;

static void binclip_into(const L *polyline, Pred func, LL *out_t, LL *out_f) {
  if (!polyline->n) return;
  Mark m = scratch_mark();
  int n = polyline->n;
  int *bins = smalloc(sizeof(int) * n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    bins[i] = func.f(func.ctx, polyline->a[i]->x, polyline->a[i]->y, t);
  }
  Sides out = {{out_f, out_t}, {NULL, NULL}};
  int io = bins[0];
  for (int i = 0; i < n; i++) {
    P *a = polyline->a[i];
    side_push(&out, io, a);
    if (i + 1 >= n) break;
    P *b = polyline->a[i + 1];
    if (bins[i] != bins[i + 1]) {
      P *pt = lerp2dp(a, b, 0.5);
      side_push(&out, io, pt);
      io = !io;
      side_start(&out, io, pt);
    }
  }
  scratch_reset(m);
}

static LL *binclip_in(const LL *polylines, Pred func) {
  LL *out = LL_new(polylines->n);
  for (int i = 0; i < polylines->n; i++) binclip_into(polylines->a[i], func, out, NULL);
  return out;
}

// ---------------------------------------------------------------------------
// Transforms (all make new points, like the JS map()s)

static L *trsl_poly(const L *poly, double x, double y) {
  L *o = L_new(poly->n);
  for (int i = 0; i < poly->n; i++) L_push(o, P_new(poly->a[i]->x + x, poly->a[i]->y + y));
  return o;
}
static LL *trsl_polys(const LL *polys, double x, double y) {
  LL *o = LL_new(polys->n);
  for (int i = 0; i < polys->n; i++) LL_push(o, trsl_poly(polys->a[i], x, y));
  return o;
}
static L *rot_poly(const L *poly, double th) {
  L *o = L_new(poly->n);
  double costh = fd_cos(th), sinth = fd_sin(th);
  for (int i = 0; i < poly->n; i++) {
    double x0 = poly->a[i]->x, y0 = poly->a[i]->y;
    L_push(o, P_new(x0 * costh - y0 * sinth, x0 * sinth + y0 * costh));
  }
  return o;
}

// ---------------------------------------------------------------------------
// Resampling and simplification

static int isect_circ_line(double cx, double cy, double r, double x0, double y0, double x1, double y1, double *out) {
  double dx = x1 - x0, dy = y1 - y0, fx = x0 - cx, fy = y0 - cy;
  double a = dx * dx + dy * dy;
  double b = 2 * (fx * dx + fy * dy);
  double c = (fx * fx + fy * fy) - r * r;
  double discriminant = b * b - 4 * a * c;
  if (discriminant < 0) return 0;
  discriminant = sqrt(discriminant);
  double t0 = (-b - discriminant) / (2 * a);
  if (0 <= t0 && t0 <= 1) {
    *out = t0;
    return 1;
  }
  double t = (-b + discriminant) / (2 * a);
  if (t > 1 || t < 0) return 0;
  *out = t;
  return 1;
}

static L *resample(const L *polyline_in, double step) {
  if (polyline_in->n < 2) return L_copy(polyline_in);
  Mark mark = scratch_mark();
  L copy = {smalloc(sizeof(P *) * polyline_in->n), polyline_in->n, polyline_in->n, A_SCRATCH};
  memcpy(copy.a, polyline_in->a, sizeof(P *) * polyline_in->n);
  L *polyline = &copy;
  L *out = L_new(polyline->n * 2);
  L_push(out, P_copy(polyline->a[0]));
  int i = 0;
  while (i < polyline->n - 1) {
    P *a = polyline->a[i], *b = polyline->a[i + 1];
    double dx = b->x - a->x, dy = b->y - a->y;
    double d = sqrt(dx * dx + dy * dy);
    if (d == 0) {
      i++;
      continue;
    }
    int n = toi32(d / step);
    double rest = (n * step) / d;
    double rpx = a->x * (1 - rest) + b->x * rest;
    double rpy = a->y * (1 - rest) + b->y * rest;
    for (int j = 1; j <= n; j++) {
      double t = (double)j / n;
      L_push(out, P_new(a->x * (1 - t) + rpx * t, a->y * (1 - t) + rpy * t));
    }
    int next = -1;
    for (int j = i + 2; j < polyline->n; j++) {
      P *pb = polyline->a[j - 1], *pc = polyline->a[j];
      if (pb->x == pc->x && pb->y == pc->y) continue;
      double t;
      if (!isect_circ_line(rpx, rpy, step, pb->x, pb->y, pc->x, pc->y, &t)) continue;
      P *q = P_new(pb->x * (1 - t) + pc->x * t, pb->y * (1 - t) + pc->y * t);
      L_push(out, q);
      polyline->a[j - 1] = q;
      next = j - 1;
      break;
    }
    if (next < 0) break;
    i = next;
  }
  if (out->n > 1) {
    double lx = out->a[out->n - 1]->x, ly = out->a[out->n - 1]->y;
    double mx = polyline->a[polyline->n - 1]->x, my = polyline->a[polyline->n - 1]->y;
    double d = sqrt(fd_pow(mx - lx, 2) + fd_pow(my - ly, 2));
    if (d < step * 0.5) out->n--;
  }
  L_push(out, P_copy(polyline->a[polyline->n - 1]));
  scratch_reset(mark);
  return out;
}

static double pt_seg_dist(double x, double y, double x1, double y1, double x2, double y2) {
  double A = x - x1, B = y - y1, C = x2 - x1, D = y2 - y1;
  double dot = A * C + B * D;
  double len_sq = C * C + D * D;
  double param = -1;
  if (len_sq != 0) param = dot / len_sq;
  double xx, yy;
  if (param < 0) {
    xx = x1;
    yy = y1;
  } else if (param > 1) {
    xx = x2;
    yy = y2;
  } else {
    xx = x1 + param * C;
    yy = y1 + param * D;
  }
  double dx = x - xx, dy = y - yy;
  return sqrt(dx * dx + dy * dy);
}

// approx_poly_dp on pts[lo..hi]. Returns a scratch array that, like the JS,
// holds original points where a sub-slice of <= 2 points is returned as is,
// and copies where the JS calls slice() on a point.
typedef struct { P **a; int n; } PA;
static PA approx_rec(P **pts, int lo, int hi, double epsilon) {
  int n = hi - lo + 1;
  PA r;
  if (n <= 2) {
    r.a = pts + lo;
    r.n = n;
    return r;
  }
  double dmax = 0;
  int argmax = -1;
  const P *p0 = pts[lo], *pn = pts[hi];
  for (int i = lo + 1; i < hi; i++) {
    double d = pt_seg_dist(pts[i]->x, pts[i]->y, p0->x, p0->y, pn->x, pn->y);
    if (d > dmax) {
      dmax = d;
      argmax = i;
    }
  }
  if (dmax > epsilon) {
    PA Lp = approx_rec(pts, lo, argmax, epsilon);
    PA R = approx_rec(pts, argmax, hi, epsilon);
    r.n = (Lp.n - 1) + R.n;
    r.a = smalloc(sizeof(P *) * r.n);
    memcpy(r.a, Lp.a, sizeof(P *) * (Lp.n - 1));
    memcpy(r.a + Lp.n - 1, R.a, sizeof(P *) * R.n);
    return r;
  }
  r.a = smalloc(sizeof(P *) * 2);
  r.a[0] = P_copy(p0);
  r.a[1] = P_copy(pn);
  r.n = 2;
  return r;
}

static L *approx_poly_dp(L *polyline, double epsilon) {
  if (polyline->n <= 2) return polyline;
  Mark m = scratch_mark();
  PA r = approx_rec(polyline->a, 0, polyline->n - 1, epsilon);
  L *out = L_new(r.n);
  for (int i = 0; i < r.n; i++) L_push(out, r.a[i]);
  scratch_reset(m);
  return out;
}

// ---------------------------------------------------------------------------
// Poisson disk sampling (samples is an L of fresh points)

static void poissondisk(double W, double H, double r, L *samples) {
  double w = r / 1.4142135624;
  double r2 = r * r;
  int cols = toi32(W / w), rows = toi32(H / w);
  int ncell = cols * rows > 0 ? cols * rows : 0;
  Mark m = scratch_mark();
  int *grid = smalloc(sizeof(int) * (ncell ? ncell : 1));
  for (int i = 0; i < ncell; i++) grid[i] = -1;
  int la = list_arena;
  list_arena = A_SCRATCH;
  L *active = L_new(64);
  list_arena = la;
  L_push(samples, P_new(W / 2.0, H / 2.0));
  for (int i = 0; i < samples->n; i++) {
    int col = toi32(samples->a[i]->x / w), row = toi32(samples->a[i]->y / w);
    int gi = col + row * cols;
    if (gi < 0 || gi >= ncell) fail(); // the JS would grow the array; never happens here
    grid[gi] = i;
    L_push(active, samples->a[i]);
  }
  while (active->n) {
    int ridx = toi32(rnd() * active->n);
    P *pos = active->a[ridx];
    int found = 0;
    for (int n = 0; n < 30; n++) {
      double sr = r + rnd() * r;
      double sa = 6.2831853072 * rnd();
      double sx = pos->x + sr * fd_cos(sa);
      double sy = pos->y + sr * fd_sin(sa);
      int col = toi32(sx / w), row = toi32(sy / w);
      if (col > 0 && row > 0 && col < cols - 1 && row < rows - 1 && grid[col + row * cols] == -1) {
        int ok = 1;
        for (int i = -1; i <= 1; i++) {
          for (int j = -1; j <= 1; j++) {
            int idx = (row + i) * cols + col + j;
            int nbr = grid[idx];
            if (-1 != nbr) {
              double dx = sx - samples->a[nbr]->x, dy = sy - samples->a[nbr]->y;
              if (dx * dx + dy * dy < r2) ok = 0;
            }
          }
        }
        if (ok) {
          found = 1;
          grid[row * cols + col] = samples->n;
          P *sample = P_new(sx, sy);
          L_push(active, sample);
          L_push(samples, sample);
        }
      }
    }
    if (!found) {
      memmove(active->a + ridx, active->a + ridx + 1, sizeof(P *) * (active->n - ridx - 1));
      active->n--;
    }
  }
  scratch_reset(m);
}

static void LL_unshift(LL *l, L *p) {
  LL_push(l, p);
  memmove(l->a + 1, l->a, sizeof(L *) * (l->n - 1));
  l->a[0] = p;
}
static L *rev_copy(const L *l) { return L_reverse(L_copy(l)); }
static double deviate(double n) { return rnd() * 2 * n - n; }
static double pow_s(double a, double b) { return js_sign(a) * fd_pow(fabs(a), b); } // fishdraw's pow()
static double gauss2d(double x, double y) {
  double z0 = fd_exp(-0.5 * x * x);
  double z1 = fd_exp(-0.5 * y * y);
  return z0 * z1;
}

// ---------------------------------------------------------------------------
// Parameters

typedef struct {
  double body_curve_type, body_curve_amount, body_length, body_height, scale_type, scale_scale, pattern_type,
      pattern_scale, dorsal_texture_type, dorsal_type, dorsal_length, dorsal_start, dorsal_end, wing_texture_type,
      wing_type, wing_start, wing_end, wing_y, wing_length, wing_width, pelvic_start, pelvic_end, pelvic_length,
      pelvic_type, pelvic_texture_type, anal_start, anal_end, anal_length, anal_type, anal_texture_type, tail_type,
      tail_length, finlet_type, neck_type, nose_height, mouth_size, head_length, head_texture_amount, has_moustache,
      moustache_length, has_beard, has_teeth, teeth_length, teeth_space, beard_length, eye_type, eye_size, jaw_size,
      jaw_open;
} Arg;

// ---------------------------------------------------------------------------
// Patterns on the body: pattern_func in the JS.

typedef struct {
  int kind; // 1: dots, 2: noise patches, 3: stripes
  double scale;
  L *samples;
  double *rs;
} Pattern;

static int pattern_eval(const Pattern *pf, double x, double y) {
  if (pf->kind == 1) {
    for (int i = 0; i < pf->samples->n; i++) {
      double r = pf->rs[i];
      const P *s = pf->samples->a[i];
      if (dist(x, y, s->x, s->y) < r) {
        double dx = x - s->x, dy = y - s->y;
        if (gauss2d(dx / r * 2, dy / r * 2) * noise(x, y, 999) > 0.2) return 1;
      }
    }
    return 0;
  }
  if (pf->kind == 2) return (noise2(x * 0.1, y * 0.1) * js_max(0.35, (y - 10) / 280)) < 0.2;
  double dx = noise2(x * 0.01, y * 0.01) * 30;
  return (toi32((x + dx) / (30 * pf->scale))) % 2 == 1;
}
static int pattern_pred(void *ctx, double x, double y, double t) {
  (void)t;
  return pattern_eval(ctx, x, y);
}

static void pattern_dot(Pattern *pf, double scale) {
  pf->kind = 1;
  pf->samples = L_new(64);
  poissondisk(500, 300, 20 * scale, pf->samples);
  pf->rs = amalloc(sizeof(double) * (pf->samples->n ? pf->samples->n : 1));
  for (int i = 0; i < pf->samples->n; i++) pf->rs[i] = (rnd() * 5 + 10) * scale;
}

// ---------------------------------------------------------------------------
// Shading

static LL *diagonal_lines(BBox b, double from, double to, double step, double dx1, int slope) {
  (void)slope;
  LL *lines = LL_new(64);
  for (double i = from; i < to; i += step) {
    LL_push(lines, L_of(2, P_new(b.x + i, b.y), P_new(b.x + i + dx1, b.y + b.h)));
  }
  return lines;
}

static LL *shade_shape(const L *poly, double step, double dx, double dy) {
  BBox b = get_bbox(poly);
  b.x -= step;
  b.y -= step;
  b.w += step * 2;
  b.h += step * 2;
  LL *lines = diagonal_lines(b, -b.h, b.w, step, b.h, 0);
  lines = clip_in(lines, poly);
  L *carve = trsl_poly(poly, -dx, -dy);
  lines = clip_out(lines, carve);
  for (int i = 0; i < lines->n; i++) {
    L *ln = lines->a[i];
    if (ln->n < 2) fail(); // spreading undefined in the JS
    P *a = ln->a[0], *bb = ln->a[1];
    double s = rnd() * 0.5;
    if (dy > 0) {
      a = lerp2dp(a, bb, s);
      ln->a[0] = a;
    } else {
      bb = lerp2dp(bb, a, s);
      ln->a[1] = bb;
    }
  }
  return lines;
}

static LL *fill_shape(const L *poly, double step) {
  BBox b = get_bbox(poly);
  b.x -= step;
  b.y -= step;
  b.w += step * 2;
  b.h += step * 2;
  LL *lines = LL_new(64);
  for (double i = 0; i < b.w + b.h / 2; i += step) {
    LL_push(lines, L_of(2, P_new(b.x + i, b.y), P_new(b.x + i - b.h / 2, b.y + b.h)));
  }
  return clip_in(lines, poly);
}

static LL *patternshade_shape(const L *poly, double step, const Pattern *pf) {
  BBox b = get_bbox(poly);
  b.x -= step;
  b.y -= step;
  b.w += step * 2;
  b.h += step * 2;
  LL *lines = LL_new(64);
  for (double i = -b.h / 2; i < b.w; i += step) {
    LL_push(lines, L_of(2, P_new(b.x + i, b.y), P_new(b.x + i + b.h / 2, b.y + b.h)));
  }
  lines = clip_in(lines, poly);
  for (int i = 0; i < lines->n; i++) lines->a[i] = resample(lines->a[i], 2);
  Pred pred = {pattern_pred, (void *)pf};
  return binclip_in(lines, pred);
}

static LL *vein_shape(const L *poly, double n) {
  BBox b = get_bbox(poly);
  LL *out = LL_new(64);
  for (int i = 0; i < n; i++) {
    double x = b.x + rnd() * b.w;
    double y = b.y + rnd() * b.h;
    L *o = L_new(16);
    L_push(o, P_new(x, y));
    for (int j = 0; j < 15; j++) {
      double dx = (noise(x * 0.1, y * 0.1, 7) - 0.5) * 4;
      double dy = (noise(x * 0.1, y * 0.1, 6) - 0.5) * 4;
      x += dx;
      y += dy;
      L_push(o, P_new(x, y));
    }
    LL_push(out, o);
  }
  return clip_in(out, poly);
}

// The samples and the dots' lists are only needed until the dots are
// clipped, so they're built in the STAGE arena; each dot's untranslated
// shape is built in scratch. The dots' points stay in MAIN: clipping shares
// them with its output.
static LL *smalldot_shape(const L *poly, double scale) {
  Mark stage = mark_of(A_STAGE);
  int la = list_arena, pa = point_arena;
  list_arena = point_arena = A_STAGE;
  L *samples = L_new(64);
  BBox b = get_bbox(poly);
  poissondisk(b.w, b.h, 5 * scale, samples);
  for (int i = 0; i < samples->n; i++) {
    samples->a[i]->x += b.x;
    samples->a[i]->y += b.y;
  }
  point_arena = pa;
  LL *out = LL_new(64);
  int n = 7;
  for (int i = 0; i < samples->n; i++) {
    double x = samples->a[i]->x, y = samples->a[i]->y;
    double t = (y > 0) ? (y / 300) : 0.5;
    if ((t > 0.4 || y < 0) && t > rnd()) continue;
    for (int k = 0; k < 2; k++) {
      Mark m = scratch_mark();
      list_arena = point_arena = A_SCRATCH;
      L *o = L_new(n);
      for (int j = 0; j < n; j++) {
        double tt = (double)j / (n - 1);
        double a = tt * PI * 2;
        L_push(o, P_new(fd_cos(a) * 1 - k * 0.3, fd_sin(a) * 0.5 - k * 0.3));
      }
      double th = rnd() * PI * 2;
      L *r = rot_poly(o, th);
      list_arena = A_STAGE;
      point_arena = pa;
      LL_push(out, trsl_poly(r, x, y));
      scratch_reset(m);
    }
  }
  list_arena = la;
  LL *res = clip_in(out, poly);
  reset_to(stage);
  return res;
}

// ---------------------------------------------------------------------------
// Scales

static L *squama_mask(double w, double h) {
  L *p = L_new(7);
  int n = 7;
  for (int i = 0; i < n; i++) {
    double t = (double)i / n;
    double a = t * PI * 2;
    double x = -pow_s(fd_cos(a), 1.3) * w;
    double y = pow_s(fd_sin(a), 1.3) * h;
    L_push(p, P_new(x, y));
  }
  return p;
}

static LL *squama(double w, double h, int m) {
  L *p = L_new(8);
  int n = 8;
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = t * PI + PI / 2;
    double x = -pow_s(fd_cos(a), 1.4) * w;
    double y = pow_s(fd_sin(a), 1.4) * h;
    L_push(p, P_new(x, y));
  }
  LL *q = LL_new(m + 1);
  LL_push(q, p);
  for (int i = 0; i < m; i++) {
    double t = (double)i / (m - 1);
    double r1 = rnd();
    double r2 = rnd();
    double r3 = rnd();
    double r4 = rnd();
    LL_push(q, L_of(2, P_new(-w * 0.3 + (r1 - 0.5), -h * 0.2 + t * h * 0.4 + (r2 - 0.5)),
                    P_new(w * 0.5 + (r3 - 0.5), -h * 0.3 + t * h * 0.6 + (r4 - 0.5))));
  }
  return q;
}

typedef struct {
  int kind; // 0: fish_body_a's, 1: fish_body_b's
  const Pattern *pf;
} SqFn;

static LL *squama_call(const SqFn *f, double x, double y, double w, double h) {
  if (f->kind == 0) {
    if (f->pf) {
      int b = pattern_eval(f->pf, x, y);
      return squama(w, h, b * 3);
    }
    return squama(w, h, 3);
  }
  return squama(w * 0.7, h * 0.6, 0);
}

// Place one scale. The JS builds the scale, then a translated copy (and the
// mask q, used only when interclip); the untranslated parts are built in
// scratch here and dropped straight away. fish_body_b translates its whole
// mesh once more; `fuse` does both translations in one step, in the same
// order of operations, so the numbers are the same.
typedef struct {
  const SqFn *sqf;
  int interclip, fuse;
  double ox, oy;
  L *clipper;
} Mesh;

static void mesh_place(LL *out, Mesh *mesh, double x, double y, double dw, double dh) {
  Mark m = scratch_mark();
  int la = list_arena, pa = point_arena;
  list_arena = point_arena = A_SCRATCH;
  L *mask = mesh->interclip ? squama_mask(dw, dh) : NULL;
  LL *raw = squama_call(mesh->sqf, x, y, dw, dh);
  list_arena = la;
  point_arena = pa;

  if (!mesh->interclip) {
    for (int k = 0; k < raw->n; k++) {
      const L *r = raw->a[k];
      L *t = L_new(r->n);
      for (int i = 0; i < r->n; i++) {
        if (mesh->fuse) L_push(t, P_new((r->a[i]->x + x) + mesh->ox, (r->a[i]->y + y) + mesh->oy));
        else L_push(t, P_new(r->a[i]->x + x, r->a[i]->y + y));
      }
      LL_push(out, t);
    }
  } else {
    L *q = trsl_poly(mask, x, y);
    LL *p = trsl_polys(raw, x, y);
    if (mesh->clipper) {
      for (int i = 0; i < p->n; i++) clip_into(p->a[i], mesh->clipper, NULL, out);
      mesh->clipper = poly_union_heap(mesh->clipper, q);
    } else {
      LL_push_all(out, p);
      mesh->clipper = q;
    }
  }
  scratch_reset(m);
}

static LL *squama_mesh(int m, int n, double uw, double uh, Mesh *mesh, double noise_x, double noise_y) {
  Mark mark = scratch_mark();
  int np = m * n > 0 ? m * n : 0;
  double *px = smalloc(sizeof(double) * (np ? np : 1));
  double *py = smalloc(sizeof(double) * (np ? np : 1));
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < m; j++) {
      double x = j * uw;
      double y = (n * uh / 2) - fd_cos((double)i / (n - 1) * PI) * (n * uh / 2);
      double a = noise2(x * 0.005, y * 0.005) * PI * 2 - PI;
      double r = noise2(x * 0.005, y * 0.005);
      double dx = fd_cos(a) * r * noise_x;
      double dy = fd_cos(a) * r * noise_y;
      px[i * m + j] = x + dx;
      py[i * m + j] = y + dy;
    }
  }
  double *whw = smalloc(sizeof(double) * (np ? np : 1));
  double *whh = smalloc(sizeof(double) * (np ? np : 1));
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < m; j++) {
      int k = i * m + j;
      if (i == 0 || j == 0 || i == n - 1 || j == m - 1) {
        whw[k] = uw / 2;
        whh[k] = uh / 2;
        continue;
      }
      int b = k + 1, c = k - 1, d = (i - 1) * m + j, e = (i + 1) * m + j;
      whw[k] = (dist(px[k], py[k], px[b], py[b]) + dist(px[k], py[k], px[c], py[c])) / 4;
      whh[k] = (dist(px[k], py[k], px[d], py[d]) + dist(px[k], py[k], px[e], py[e])) / 4;
    }
  }
  LL *out = LL_new(256);
  mesh->clipper = NULL;
  for (int j = 1; j < m - 1; j++) {
    for (int i = 1; i < n - 1; i++) {
      int k = i * m + j;
      mesh_place(out, mesh, px[k], py[k], whw[k], whh[k]);
    }
    for (int i = 1; i < n - 1; i++) {
      int a = i * m + j, b = i * m + j + 1, c = (i + 1) * m + j, d = (i + 1) * m + j + 1;
      double x = (px[a] + px[b] + px[c] + px[d]) / 4, y = (py[a] + py[b] + py[c] + py[d]) / 4;
      double dw = (whw[a] + whw[b] + whw[c] + whw[d]) / 4, dh = (whh[a] + whh[b] + whh[c] + whh[d]) / 4;
      dw *= 1.2;
      mesh_place(out, mesh, x, y, dw, dh);
    }
  }
  mem_free(heap_lists[0]);
  heap_lists[0] = NULL;
  scratch_reset(mark);
  return out;
}

// ---------------------------------------------------------------------------
// Bodies

static LL *body_o(const L *curve0, const L *curve1, const LL *a, const LL *b) {
  LL *o = LL_new(2 + a->n + (b ? b->n : 0));
  LL_push(o, (L *)curve0);
  LL_push(o, rev_copy(curve1));
  LL_push_all(o, a);
  if (b) LL_push_all(o, b);
  return o;
}

static L *lerp_curve(const L *c0, const L *c1, double t) {
  L *o = L_new(c0->n);
  for (int i = 0; i < c0->n; i++) L_push(o, lerp2dp(c0->a[i], c1->a[i], t));
  return o;
}

static LL *fish_body_a(L *curve0, L *curve1, double scale_scale, const Pattern *pf) {
  L *curve2 = lerp_curve(curve0, curve1, 0.95);
  L *curve3 = lerp_curve(curve0, curve1, 0.85);
  L *outline2 = L_concat(curve0, rev_copy(curve2));
  L *outline3 = L_concat(curve0, rev_copy(curve3));
  BBox bbox = get_bbox(L_concat(curve0, curve1));
  int m = toi32(bbox.w / (scale_scale * 15));
  int n = toi32(bbox.h / (scale_scale * 15));
  double uw = bbox.w / m, uh = bbox.h / n;
  SqFn f = {0, pf};
  Mesh mesh = {&f, 1, 0, 0, 0, NULL};
  // The mesh is only needed until it's copied (translated), so it's built
  // in the STAGE arena.
  Mark stage = mark_of(A_STAGE);
  list_arena = point_arena = A_STAGE;
  LL *raw = squama_mesh(m, n + 3, uw, uh, &mesh, uw * 3, uh * 3);
  list_arena = point_arena = A_MAIN;
  LL *sq = trsl_polys(raw, bbox.x, bbox.y - uh * 1.5);
  reset_to(stage);
  LL *o0 = clip_in(sq, outline2);
  Split o1 = clip_multi(o0, outline3);
  LL *kept = LL_new(o1.f->n);
  for (int i = 0; i < o1.f->n; i++)
    if (rnd() < 0.6) LL_push(kept, o1.f->a[i]);
  return body_o(curve0, curve1, o1.t, kept);
}

static LL *fish_body_b(L *curve0, L *curve1, double scale_scale, const Pattern *pf) {
  L *curve2 = lerp_curve(curve0, curve1, 0.95);
  L *outline2 = L_concat(curve0, rev_copy(curve2));
  BBox bbox = get_bbox(L_concat(curve0, curve1));
  int m = toi32(bbox.w / (scale_scale * 5));
  int n = toi32(bbox.h / (scale_scale * 5));
  double uw = bbox.w / m, uh = bbox.h / n;
  SqFn f = {1, NULL};
  Mesh mesh = {&f, 0, 1, bbox.x, bbox.y - uh * 8, NULL};
  // Translated in place (fuse); the lists are only needed for the clip.
  Mark stage = mark_of(A_STAGE);
  list_arena = A_STAGE;
  LL *sq = squama_mesh(m, n + 16, uw, uh, &mesh, uw * 8, uh * 8);
  list_arena = A_MAIN;
  LL *o0 = clip_in(sq, outline2);
  reset_to(stage);
  LL *o1 = LL_new(o0->n);
  for (int i = 0; i < o0->n; i++) {
    double x = o0->a[i]->a[0]->x, y = o0->a[i]->a[0]->y;
    double t = (y - bbox.y) / bbox.h;
    int keep;
    if (pf) {
      keep = pattern_eval(pf, x, y);
      if (!keep && rnd() > t) keep = rnd() > t;
    } else {
      keep = rnd() > t;
    }
    if (keep) LL_push(o1, o0->a[i]);
  }
  return body_o(curve0, curve1, o1, NULL);
}

static int pred_body_c(void *ctx, double x, double y, double t) {
  (void)ctx;
  (void)x;
  (void)y;
  return rnd() > t || rnd() > t;
}

static LL *fish_body_c(L *curve0, L *curve1, double scale_scale) {
  double step = 6 * scale_scale;
  L *curve2 = lerp_curve(curve0, curve1, 0.95);
  L *curve3 = lerp_curve(curve0, curve1, 0.4);
  L *outline2 = L_concat(curve0, rev_copy(curve2));
  BBox bbox = get_bbox(L_concat(curve0, curve1));
  bbox.x -= step;
  bbox.y -= step;
  bbox.w += step * 2;
  bbox.h += step * 2;
  LL *lines = LL_new(128);
  LL_push(lines, L_reverse(curve3));
  for (double i = -bbox.h; i < bbox.w; i += step) {
    LL_push(lines, L_of(2, P_new(bbox.x + i, bbox.y), P_new(bbox.x + i + bbox.h, bbox.y + bbox.h)));
  }
  for (double i = 0; i < bbox.w + bbox.h; i += step) {
    LL_push(lines, L_of(2, P_new(bbox.x + i, bbox.y), P_new(bbox.x + i - bbox.h, bbox.y + bbox.h)));
  }
  for (int i = 0; i < lines->n; i++) {
    lines->a[i] = resample(lines->a[i], 4);
    for (int j = 0; j < lines->a[i]->n; j++) {
      P *pt = lines->a[i]->a[j];
      double x = pt->x, y = pt->y;
      double t = (y - bbox.y) / bbox.h;
      double y1 = -fd_cos(t * PI) * bbox.h / 2 + bbox.y + bbox.h / 2;
      double dx = (noise(x * 0.005, y1 * 0.005, 0.1) - 0.5) * 50;
      double dy = (noise(x * 0.005, y1 * 0.005, 1.2) - 0.5) * 50;
      pt->x += dx;
      pt->y = y1 + dy;
    }
  }
  LL *o0 = clip_in(lines, outline2);
  Pred pred = {pred_body_c, NULL};
  o0 = binclip_in(o0, pred);
  return body_o(curve0, curve1, o0, NULL);
}

static int pred_body_d(void *ctx, double x, double y, double t) {
  (void)ctx;
  (void)y;
  return (rnd() > fd_cos(t * PI) && rnd() < x / 500) || (rnd() > fd_cos(t * PI) && rnd() < x / 500);
}

static LL *fish_body_d(L *curve0, L *curve1, double scale_scale) {
  L *curve2 = lerp_curve(curve0, curve1, 0.4);
  curve0 = resample(curve0, 10 * scale_scale);
  curve1 = resample(curve1, 10 * scale_scale);
  curve2 = resample(curve2, 10 * scale_scale);
  L *outline1 = L_concat(curve0, rev_copy(curve1));
  LL *o0 = LL_new(64);
  LL_push(o0, curve2);
  int lim = curve0->n;
  if (curve1->n < lim) lim = curve1->n;
  if (curve2->n < lim) lim = curve2->n;
  for (int i = 3; i < lim; i++) {
    LL_push(o0, L_of(2, curve0->a[i], curve2->a[i - 3]));
    LL_push(o0, L_of(2, curve2->a[i - 3], curve1->a[i]));
  }
  LL *o1 = LL_new(64);
  Pred pred = {pred_body_d, NULL};
  for (int i = 0; i < o0->n; i++) {
    o0->a[i] = resample(o0->a[i], 4);
    for (int j = 0; j < o0->a[i]->n; j++) {
      P *pt = o0->a[i]->a[j];
      double x = pt->x, y = pt->y;
      double dx = 30 * (noise(x * 0.01, y * 0.01, -1) - 0.5);
      double dy = 30 * (noise(x * 0.01, y * 0.01, 9) - 0.5);
      pt->x += dx;
      pt->y += dy;
    }
    binclip_into(o0->a[i], pred, o1, NULL);
  }
  o1 = clip_in(o1, outline1);
  LL *sh = vein_shape(outline1, 50);
  return body_o(curve0, curve1, o1, sh);
}

// ---------------------------------------------------------------------------
// Fins

typedef enum { F0_A, F0_B, F1_A, F1_B, F2_A, F2_B, F3_A, F3_B, T0, T1, T2, T3, T4, T5, F5_3 } WKind;
typedef struct {
  WKind kind;
  const Arg *arg;
} WFn;

static double wfn(const WFn *f, double t) {
  const Arg *arg = f->arg;
  switch (f->kind) {
    case F0_A: return (0.3 + noise1(t * 3) * 0.7) * arg->dorsal_length * fd_pow(fd_sin(t * PI), 0.5);
    case F0_B: return arg->dorsal_length * ((fd_pow(t - 1, 2)) * 0.5 + (1 - t) * 0.5);
    case F1_A: return (40 + (20 + noise1(t * 3) * 70) * fd_pow(fd_sin(t * PI), 0.5)) / 130 * arg->wing_length;
    case F1_B: return arg->wing_length * (1 - t * 0.95);
    case F2_A: return (10 + (15 + noise1(t * 3) * 60) * fd_pow(fd_sin(t * PI), 0.5)) / 85 * arg->pelvic_length;
    case F2_B: return (t * 0.5 + 0.5) * arg->pelvic_length;
    case F3_A: return (10 + (10 + noise1(t * 3) * 30) * fd_pow(fd_sin(t * PI), 0.5)) / 50 * arg->anal_length;
    case F3_B: return arg->anal_length * (t * t * 0.8 + 0.2);
    case T0: return (75 - (10 + noise1(t * 3) * 10) * fd_sin(3 * t * PI - PI)) / 75 * arg->tail_length;
    case T1: return arg->tail_length * (fd_sin(t * PI) * 0.5 + 0.5);
    case T2: return (fabs(fd_cos(PI * t)) * 0.8 + 0.2) * arg->tail_length;
    case T3: return (1 - fd_sin(t * PI) * 0.3) * arg->tail_length;
    case T4: return (1 - fd_sin(t * PI) * 0.6) * (1 - t * 0.45) * arg->tail_length;
    case T5: return (1 - fd_pow(fd_sin(t * PI), 0.4) * 0.55) * arg->tail_length;
    default: return (0.3 + noise1(t * 3) * 0.7) * arg->dorsal_length * 0.6 * fd_pow(fd_sin(t * PI), 0.5);
  }
}

typedef struct {
  L *c;   // outline, for clipping
  LL *f;  // strokes
} Part;

static double *fin_angles(const L *curve) {
  int n = curve->n;
  if (n == 1) fail(); // curve[1] is undefined in the JS
  double *angs = amalloc(sizeof(double) * (n ? n : 1));
  for (int i = 0; i < n; i++) {
    const P *const *c = (const P *const *)curve->a;
    if (i == 0) {
      angs[i] = fd_atan2(c[i + 1]->y - c[i]->y, c[i + 1]->x - c[i]->x) - PI / 2;
    } else if (i == n - 1) {
      angs[i] = fd_atan2(c[i]->y - c[i - 1]->y, c[i]->x - c[i - 1]->x) - PI / 2;
    } else {
      double a0 = fd_atan2(c[i - 1]->y - c[i]->y, c[i - 1]->x - c[i]->x);
      double a1 = fd_atan2(c[i + 1]->y - c[i]->y, c[i + 1]->x - c[i]->x);
      while (a1 > a0) a1 -= PI * 2;
      a1 += PI * 2;
      angs[i] = (a0 + a1) / 2;
    }
  }
  return angs;
}

static Part fin_a(const L *curve, double ang0, double ang1, WFn func, int clip_root, double curvature0,
                  double curvature1, double softness) {
  int nc = curve->n;
  double *angs = fin_angles(curve);
  L *out0 = L_new(nc);
  LL *out1 = LL_new(nc);
  L *out2 = L_new(0), *out3 = L_new(0);
  for (int i = 0; i < nc; i++) {
    double t = (double)i / (nc - 1);
    double aa = lerp(ang0, ang1, t);
    double a = angs[i] + aa;
    double w = wfn(&func, t);
    double x0 = curve->a[i]->x, y0 = curve->a[i]->y;
    double x1 = x0 + fd_cos(a) * w;
    double y1 = y0 + fd_sin(a) * w;
    L *p = resample(L_of(2, P_new(x0, y0), P_new(x1, y1)), 3);
    for (int j = 0; j < p->n; j++) {
      double s = (double)j / (p->n - 1);
      double ss = sqrt(s);
      double x = p->a[j]->x, y = p->a[j]->y;
      double cv = lerp(curvature0, curvature1, t) * fd_sin(s * PI);
      p->a[j]->x += noise(x * 0.1, y * 0.1, 3) * ss * softness + fd_cos(a - PI / 2) * cv;
      p->a[j]->y += noise(x * 0.1, y * 0.1, 4) * ss * softness + fd_sin(a - PI / 2) * cv;
    }
    if (i == 0) {
      out2 = p;
    } else if (i == nc - 1) {
      out3 = rev_copy(p);
    } else {
      L_push(out0, p->a[p->n - 1]);
      double start = clip_root ? toi32(rnd() * 4) : 0;
      double end = js_max(2, toi32(p->n * (rnd() * 0.5 + 0.5)));
      L *q = L_slice(p, start, end);
      if (q->n) LL_push(out1, q);
    }
  }
  out0 = resample(out0, 3);
  for (int i = 0; i < out0->n; i++) {
    double x = out0->a[i]->x, y = out0->a[i]->y;
    out0->a[i]->x += (noise2(x * 0.1, y * 0.1) * 6 - 3) * (softness / 10);
    out0->a[i]->y += (noise2(x * 0.1, y * 0.1) * 6 - 3) * (softness / 10);
  }
  L *o = L_concat(L_concat(out2, out0), out3);
  LL_unshift(out1, o);
  Part r = {L_concat(o, rev_copy(curve)), out1};
  return r;
}

static Part fin_b(const L *curve, double ang0, double ang1, WFn func, double dark) {
  int nc = curve->n;
  double *angs = fin_angles(curve);
  LL *out0 = LL_new(nc), *out2 = LL_new(nc), *out3 = LL_new(nc);
  for (int i = 0; i < nc; i++) {
    double t = (double)i / (nc - 1);
    double aa = lerp(ang0, ang1, t);
    double a = angs[i] + aa;
    double w = wfn(&func, t);
    double x0 = curve->a[i]->x, y0 = curve->a[i]->y;
    double x1 = x0 + fd_cos(a) * w;
    double y1 = y0 + fd_sin(a) * w;
    P *b = P_new(x1 + 0.5 * fd_cos(a - PI / 2), y1 + 0.5 * fd_sin(a - PI / 2));
    P *c = P_new(x1 + 0.5 * fd_cos(a + PI / 2), y1 + 0.5 * fd_sin(a + PI / 2));
    P *p = P_new(curve->a[i]->x + 1.8 * fd_cos(a - PI / 2), curve->a[i]->y + 1.8 * fd_sin(a - PI / 2));
    P *q = P_new(curve->a[i]->x + 1.8 * fd_cos(a + PI / 2), curve->a[i]->y + 1.8 * fd_sin(a + PI / 2));
    LL_push(out0, L_of(4, p, b, c, q));
  }
  int n = 10;
  for (int i = 0; i < nc - 1; i++) {
    P *a0 = out0->a[i]->a[2], *q0 = out0->a[i]->a[3];
    P *p1 = out0->a[i + 1]->a[0], *a1 = out0->a[i + 1]->a[1];
    P *b = lerp2dp(a0, q0, 0.1);
    P *c = lerp2dp(a1, p1, 0.1);
    L *o = L_new(n);
    double ang = fd_atan2(c->y - b->y, c->x - b->x);
    for (int j = 0; j < n; j++) {
      double t = (double)j / (n - 1);
      double d = fd_sin(t * PI) * 2;
      P *a = lerp2dp(b, c, t);
      L_push(o, P_new(a->x + fd_cos(ang + PI / 2) * d, a->y + fd_sin(ang + PI / 2) * d));
    }
    LL_push(out2, o);
    int m = toi32(js_min(distp(a0, q0), distp(a1, p1)) / 10 * dark);
    P *e = lerp2dp(curve->a[i], curve->a[i + 1], 0.5);
    for (int k = 0; k < m; k++) {
      L *pp = L_new(n);
      double s = (double)k / m * 0.7;
      for (int j = 1; j < n - 1; j++) L_push(pp, lerp2dp(o->a[j], e, s));
      LL_push(out3, pp);
    }
  }
  LL *out4 = LL_new(nc);
  if (out0->n > 1) {
    L *clipper = out0->a[0];
    LL_push(out4, out0->a[0]);
    for (int i = 1; i < out0->n; i++) {
      clip_into(out0->a[i], clipper, NULL, out4);
      clipper = poly_union(clipper, out0->a[i]);
    }
  }
  Part r = {L_concat(LL_flat(out2), rev_copy(curve)), LL_concat(LL_concat(out4, out2), out3)};
  return r;
}

static Part finlet(const L *curve, double h, double dir) {
  int nc = curve->n;
  double *angs = fin_angles(curve);
  L *out0 = L_new(nc);
  for (int i = 0; i < nc; i++) {
    double t = (double)i / (nc - 1);
    double a = angs[i];
    double w = (i + 1) % 3 ? 0 : h;
    if (dir > 0) {
      w *= (1 - t * 0.5);
    } else {
      w *= 0.5 + t * 0.5;
    }
    double x0 = curve->a[i]->x, y0 = curve->a[i]->y;
    L_push(out0, P_new(x0 + fd_cos(a) * w, y0 + fd_sin(a) * w));
  }
  out0 = resample(out0, 2);
  for (int i = 0; i < out0->n; i++) {
    double x = out0->a[i]->x, y = out0->a[i]->y;
    out0->a[i]->x += noise2(x * 0.1, y * 0.1) * 2 - 3;
    out0->a[i]->y += noise2(x * 0.1, y * 0.1) * 2 - 3;
  }
  L_push(out0, curve->a[nc - 1]);
  Part r = {L_concat(out0, rev_copy(curve)), LL_one(out0)};
  return r;
}

static int pred_adipose(void *ctx, double x, double y, double t) {
  (void)ctx;
  (void)x;
  (void)y;
  return rnd() < fd_sin(t * PI);
}

static Part fin_adipose(const L *curve, double dx, double dy, double r) {
  int n = 20;
  const P *mid = curve->a[toi32(curve->n / 2.0)];
  double x = mid->x + dx, y = mid->y + dy;
  double x1 = curve->a[0]->x, y1 = curve->a[0]->y;
  double x2 = curve->a[curve->n - 1]->x, y2 = curve->a[curve->n - 1]->y;
  double d1 = dist(x, y, x1, y1);
  double d2 = dist(x, y, x2, y2);
  double a1 = fd_acos(r / d1);
  double a2 = fd_acos(r / d2);
  double a01 = fd_atan2(y1 - y, x1 - x) + a1;
  double a02 = fd_atan2(y2 - y, x2 - x) - a2;
  a02 -= PI * 2;
  while (a02 < a01) a02 += PI * 2;
  L *out0 = L_new(n + 2);
  L_push(out0, P_new(x1, y1));
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = lerp(a01, a02, t);
    L_push(out0, P_new(x + fd_cos(a) * r, y + fd_sin(a) * r));
  }
  L_push(out0, P_new(x2, y2));
  out0 = resample(out0, 3);
  for (int i = 0; i < out0->n; i++) {
    double t = (double)i / (out0->n - 1);
    double s = fd_sin(t * PI);
    double px = out0->a[i]->x, py = out0->a[i]->y;
    out0->a[i]->x += (noise2(px * 0.01, py * 0.01) - 0.5) * s * 50;
    out0->a[i]->y += (noise2(px * 0.01, py * 0.01) - 0.5) * s * 50;
  }
  L *cc = L_concat(out0, rev_copy(curve));
  LL *out1 = LL_new(4);
  clip_into(trsl_poly(out0, 0, 4), cc, out1, NULL);
  Pred pred = {pred_adipose, NULL};
  out1 = binclip_in(out1, pred);
  LL *f = LL_new(out1->n + 1);
  LL_push(f, out0);
  LL_push_all(f, out1);
  Part res = {cc, f};
  return res;
}

// ---------------------------------------------------------------------------
// Head

static L *fish_lip(double x0, double y0, double x1, double y1, double w) {
  x0 += rnd() * 0.001 - 0.0005;
  y0 += rnd() * 0.001 - 0.0005;
  x1 += rnd() * 0.001 - 0.0005;
  y1 += rnd() * 0.001 - 0.0005;
  double h = dist(x0, y0, x1, y1);
  double a0 = fd_atan2(y1 - y0, x1 - x0);
  int n = 10;
  double ang = fd_acos(w / h);
  double dx = fd_cos(a0 + PI / 2) * 0.5;
  double dy = fd_sin(a0 + PI / 2) * 0.5;
  L *o = L_new(n + 2);
  L_push(o, P_new(x0 - dx, y0 - dy));
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = lerp(ang, PI * 2 - ang, t) + a0;
    L_push(o, P_new(-fd_cos(a) * w + x1, -fd_sin(a) * w + y1));
  }
  L_push(o, P_new(x0 + dx, y0 + dy));
  o = resample(o, 2.5);
  for (int i = 0; i < o->n; i++) {
    double x = o->a[i]->x, y = o->a[i]->y;
    o->a[i]->x += noise(x * 0.05, y * 0.05, -1) * 2 - 1;
    o->a[i]->y += noise(x * 0.05, y * 0.05, -2) * 2 - 1;
  }
  return o;
}

static LL *fish_teeth(double x0, double y0, double x1, double y1, double h, double dir, double sep) {
  int n = (int)js_max(2, toi32(dist(x0, y0, x1, y1) / sep));
  double ang = fd_atan2(y1 - y0, x1 - x0);
  LL *out = LL_new(n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    P *a = lerp2d(x0, y0, x1, y1, t);
    double w = h * t;
    P *b = P_new(a->x + fd_cos(ang + dir * PI / 2) * w, a->y + fd_sin(ang + dir * PI / 2) * w);
    P *c = P_new(a->x + 1 * fd_cos(ang), a->y + 1 * fd_sin(ang));
    P *d = P_new(a->x + 1 * fd_cos(ang + PI), a->y + 1 * fd_sin(ang + PI));
    P *e = lerp2dp(c, b, 0.7);
    P *f = lerp2dp(d, b, 0.7);
    P *g = P_new(a->x + fd_cos(ang + dir * (PI / 2 + 0.15)) * w, a->y + fd_sin(ang + dir * (PI / 2 + 0.15)) * w);
    LL_push(out, L_of(5, c, e, g, f, d));
  }
  return out;
}

static Part fish_jaw(double x0, double y0, double x1, double y1, double x2, double y2) {
  int n = 10;
  double ang = fd_atan2(y2 - y0, x2 - x0);
  double d = dist(x0, y0, x2, y2);
  L *o = L_new(n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double s = fd_sin(t * PI);
    double w = s * d / 20;
    P *p = lerp2d(x2, y2, x0, y0, t);
    double qx = p->x + fd_cos(ang - PI / 2) * w, qy = p->y + fd_sin(ang - PI / 2) * w;
    double qqx = qx + (noise(qx * 0.01, qy * 0.01, 1) - 0.5) * 4 * s;
    double qqy = qy + (noise(qx * 0.01, qy * 0.01, 4) - 0.5) * 4 * s;
    L_push(o, P_new(qqx, qqy));
  }
  LL *f = LL_new(8);
  LL_push(f, o);
  LL_push_all(f, vein_shape(o, 5));
  Part r = {L_of(3, P_new(x2, y2), P_new(x1, y1), P_new(x0, y0)), f};
  return r;
}

static Part fish_eye_a(double ex, double ey, double rad) {
  int n = 20;
  L *eye0 = L_new(n), *eye1 = L_new(n), *eye2 = L_new(n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = t * PI * 2 + PI / 4 * 3;
    L_push(eye0, P_new(ex + fd_cos(a) * rad, ey + fd_sin(a) * rad));
    if (t > 0.5) L_push(eye1, P_new(ex + fd_cos(a) * (rad * 0.8), ey + fd_sin(a) * (rad * 0.8)));
    L_push(eye2, P_new(ex + fd_cos(a) * (rad * 0.4) - 0.75, ey + fd_sin(a) * (rad * 0.4) - 0.75));
  }
  LL *ef = shade_shape(eye2, 2.7, 10, 10);
  LL *f = LL_new(3 + ef->n);
  LL_push(f, eye0);
  LL_push(f, eye1);
  LL_push(f, eye2);
  LL_push_all(f, ef);
  Part r = {eye0, f};
  return r;
}

static Part fish_eye_b(double ex, double ey, double rad) {
  int n = 20;
  L *eye0 = L_new(n), *eye2 = L_new(n);
  LL *eye1 = LL_new(8);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = t * PI * 2 + MATH_E;
    L_push(eye0, P_new(ex + fd_cos(a) * rad, ey + fd_sin(a) * rad));
    L_push(eye2, P_new(ex + fd_cos(a) * (rad * 0.4), ey + fd_sin(a) * (rad * 0.4)));
  }
  int m = toi32((rad * 0.6) / 2);
  for (int i = 0; i < m; i++) {
    double r = rad - i * 2;
    L *e = L_new(n);
    for (int k = 0; k < n; k++) {
      double t = (double)k / (n - 1);
      double a = lerp(PI * 7 / 8, PI * 13 / 8, t);
      L_push(e, P_new(ex + fd_cos(a) * r, ey + fd_sin(a) * r));
    }
    LL_push(eye1, e);
  }
  L *trig = L_of(3, P_new(ex + fd_cos(-PI * 3 / 4) * (rad * 0.9), ey + fd_sin(-PI * 3 / 4) * (rad * 0.9)),
                 P_new(ex + 1, ey + 1),
                 P_new(ex + fd_cos(-PI * 11 / 12) * (rad * 0.9), ey + fd_sin(-PI * 11 / 12) * (rad * 0.9)));
  trig = resample(trig, 3);
  for (int i = 0; i < trig->n; i++) {
    double x = trig->a[i]->x, y = trig->a[i]->y;
    x += noise(x * 0.1, y * 0.1, 22) * 4 - 2;
    y += noise(x * 0.1, y * 0.1, 33) * 4 - 2;
    trig->a[i] = P_new(x, y);
  }
  LL *ef = fill_shape(eye2, 1.5);
  ef = clip_out(ef, trig);
  eye1 = clip_out(eye1, trig);
  LL *eye2s = clip1_out(eye2, trig);
  LL *f = LL_new(1 + eye1->n + eye2s->n + ef->n);
  LL_push(f, eye0);
  LL_push_all(f, eye1);
  LL_push_all(f, eye2s);
  LL_push_all(f, ef);
  Part r = {eye0, f};
  return r;
}

static L *barbel(double x, double y, double n, double ang, double dd) {
  L *curve = L_new(32);
  L_push(curve, P_new(x, y));
  double sd = rnd() * PI * 2;
  double ar = 1;
  for (int i = 0; i < n; i++) {
    x += fd_cos(ang) * dd;
    y += fd_sin(ang) * dd;
    ang += (noise2(i * 0.1, sd) - 0.5) * ar;
    if (i < n / 2) {
      ar *= 1.02;
    } else {
      ar *= 0.92;
    }
    L_push(curve, P_new(x, y));
  }
  L *o0 = L_new(32), *o1 = L_new(32);
  for (int i = 0; i < n - 1; i++) {
    double t = i / (n - 1);
    double w = 1.5 * (1 - t);
    const P *a = i > 0 ? curve->a[i - 1] : NULL, *b = curve->a[i], *c = curve->a[i + 1];
    double a1 = fd_atan2(c->y - b->y, c->x - b->x);
    double a2;
    if (a) {
      double a0 = fd_atan2(a->y - b->y, a->x - b->x);
      a1 -= PI * 2;
      while (a1 < a0) a1 += PI * 2;
      a2 = (a0 + a1) / 2;
    } else {
      a2 = a1 - PI / 2;
    }
    L_push(o0, P_new(b->x + fd_cos(a2) * w, b->y + fd_sin(a2) * w));
    L_push(o1, P_new(b->x + fd_cos(a2 + PI) * w, b->y + fd_sin(a2 + PI) * w));
  }
  L_push(o0, curve->a[curve->n - 1]);
  return L_concat(o0, rev_copy(o1));
}

static Part fish_head(double x0, double y0, double x1, double y1, double x2, double y2, Arg *arg) {
  int n = 20;
  L *curve0 = L_new(n), *curve1 = L_new(n), *curve2 = L_new(n);
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = PI / 2 * t;
    double x = x1 - pow_s(fd_cos(a), 1.5) * (x1 - x0);
    double y = y0 - pow_s(fd_sin(a), 1.5) * (y0 - y1);
    double dx = (noise(x * 0.01, y * 0.01, 9) * 40 - 20) * (1.01 - t);
    double dy = (noise(x * 0.01, y * 0.01, 8) * 40 - 20) * (1.01 - t);
    L_push(curve0, P_new(x + dx, y + dy));
  }
  for (int i = 0; i < n; i++) {
    double t = (double)i / (n - 1);
    double a = PI / 2 * t;
    double x = x2 - pow_s(fd_cos(a), 0.8) * (x2 - x0);
    double y = y0 + pow_s(fd_sin(a), 1.5) * (y2 - y0);
    double dx = (noise(x * 0.01, y * 0.01, 9) * 40 - 20) * (1.01 - t);
    double dy = (noise(x * 0.01, y * 0.01, 8) * 40 - 20) * (1.01 - t);
    L_unshift(curve1, P_new(x + dx, y + dy));
  }
  double ang = fd_atan2(y2 - y1, x2 - x1);
  for (int i = 1; i < n - 1; i++) {
    double t = (double)i / (n - 1);
    P *p = lerp2d(x1, y1, x2, y2, t);
    double s = pow_s(fd_sin(t * PI), 0.5);
    double r = noise2(t * 2, 1.2) * s * 20;
    double dx = fd_cos(ang - PI / 2) * r;
    double dy = fd_sin(ang - PI / 2) * r;
    L_push(curve2, P_new(p->x + dx, p->y + dy));
  }
  L *outline = L_concat(L_concat(curve0, curve2), curve1);

  L *inl = L_slice(L_concat(L_slice(curve2, toi32(curve2->n / 3.0), curve2->n), L_slice(curve1, 0, toi32(curve1->n / 2.0))),
                   0, curve0->n);
  for (int i = 0; i < inl->n; i++) {
    double t = (double)i / (inl->n - 1);
    double s = fd_pow(fd_sin(t * PI), 2) * 0.1 + 0.12;
    inl->a[i] = lerp2dp(inl->a[i], curve0->a[i], s);
  }
  double dix = (x0 - inl->a[inl->n - 1]->x) * 0.3;
  double diy = (y0 - inl->a[inl->n - 1]->y) * 0.2;
  for (int i = 0; i < inl->n; i++) {
    inl->a[i]->x += dix;
    inl->a[i]->y += diy;
  }

  double ex = x0 * 0.475 + x1 * 0.375 + x2 * (1 - 0.475 - 0.375);
  double ey = y0 * 0.475 + y1 * 0.375 + y2 * (1 - 0.475 - 0.375);
  double d0 = pt_seg_dist(ex, ey, x0, y0, x1, y1);
  double d1 = pt_seg_dist(ex, ey, x0, y0, x2, y2);
  if (d0 < arg->eye_size && d1 < arg->eye_size) {
    arg->eye_size = js_min(d0, d1);
  } else if (d0 < arg->eye_size) {
    double ang2 = fd_atan2(y1 - y0, x1 - x0) + PI / 2;
    ex = x0 * 0.5 + x1 * 0.5 + fd_cos(ang2) * arg->eye_size;
    ey = y0 * 0.5 + y1 * 0.5 + fd_sin(ang2) * arg->eye_size;
  }

  int ms = (int)arg->mouth_size;
  P *jaw_pt0 = curve1->a[18 - ms];
  P *c18 = curve1->a[18];
  double jaw_l = distp(jaw_pt0, c18) * arg->jaw_size;
  double jaw_ang0 = fd_atan2(c18->y - jaw_pt0->y, c18->x - jaw_pt0->x);
  double jaw_ang = jaw_ang0 - (arg->has_teeth * 0.5 + 0.5) * arg->jaw_open * PI / 4;
  P *jaw_pt1 = P_new(jaw_pt0->x + fd_cos(jaw_ang) * jaw_l, jaw_pt0->y + fd_sin(jaw_ang) * jaw_l);

  Part eye = arg->eye_type ? fish_eye_b(ex, ey, arg->eye_size) : fish_eye_a(ex, ey, arg->eye_size);
  L *eye0 = eye.c;
  LL *ef = clip_in(eye.f, outline);
  LL *inlines = clip1_out(inl, eye0);

  L *lip0 = fish_lip(jaw_pt0->x, jaw_pt0->y, c18->x, c18->y, 3);
  L *lip1 = fish_lip(jaw_pt0->x, jaw_pt0->y, jaw_pt1->x, jaw_pt1->y, 3);

  P *c15 = curve1->a[15 - ms];
  Part jw = fish_jaw(c15->x, c15->y, jaw_pt0->x, jaw_pt0->y, jaw_pt1->x, jaw_pt1->y);
  LL *jaw = clip_out(jw.f, lip1);
  jaw = clip_out(jaw, outline);

  LL *teeth0s = LL_new(0), *teeth1s = LL_new(0);
  if (arg->has_teeth) {
    LL *teeth0 = fish_teeth(jaw_pt0->x, jaw_pt0->y, c18->x, c18->y, arg->teeth_length, -1, arg->teeth_space);
    LL *teeth1 = fish_teeth(jaw_pt0->x, jaw_pt0->y, jaw_pt1->x, jaw_pt1->y, arg->teeth_length, 1, arg->teeth_space);
    teeth0s = clip_out(teeth0, lip0);
    teeth1s = clip_out(teeth1, lip1);
  }

  LL *olines = clip1_out(outline, lip0);
  LL *lip0s = clip1_out(lip0, lip1);

  LL *sh = shade_shape(outline, 6, -6, -6);
  sh = clip_out(sh, lip0);
  sh = clip_out(sh, eye0);

  LL *sh2 = vein_shape(outline, arg->head_texture_amount);
  sh2 = clip_out(sh2, lip0);
  sh2 = clip_out(sh2, eye0);

  LL *bbs = LL_new(4);
  LL *lip1s = LL_one(lip1);
  if (arg->has_moustache) {
    L *bb0 = barbel(jaw_pt0->x, jaw_pt0->y, arg->moustache_length, PI * 3 / 4, 1.5);
    lip1s = clip1_out(lip1, bb0);
    jaw = clip_out(jaw, bb0);
    LL_push(bbs, bb0);
  }
  if (arg->has_beard) {
    P *jaw_pt = (jaw->n && jaw->a[0]->n) ? jaw->a[0]->a[toi32(jaw->a[0]->n / 2.0)] : curve1->a[8];
    L *bb[3];
    for (int k = 0; k < 3; k++) {
      double ra = rnd();
      L *b = barbel(jaw_pt->x, jaw_pt->y, arg->beard_length, PI * 0.6 + ra * 0.4 - 0.2, 3);
      double r2 = rnd();
      double r3 = rnd();
      bb[k] = trsl_poly(b, r2 * 1 - 0.5, r3 * 1 - 0.5);
    }
    LL *bb3c = clip_out(LL_one(bb[2]), bb[1]);
    bb3c = clip_out(bb3c, bb[0]);
    LL *bb2c = clip_out(LL_one(bb[1]), bb[0]);
    LL_push(bbs, bb[0]);
    LL_push_all(bbs, bb2c);
    LL_push_all(bbs, bb3c);
  }

  P *c0l = curve0->a[curve0->n - 1];
  L *outlinel = L_new(curve2->n + 6);
  L_push(outlinel, P_new(0, 0));
  L_push(outlinel, P_new(c0l->x, 0));
  L_push(outlinel, c0l);
  for (int i = 0; i < curve2->n; i++) L_push(outlinel, curve2->a[i]);
  L_push(outlinel, curve1->a[0]);
  L_push(outlinel, P_new(curve1->a[0]->x, 300));
  L_push(outlinel, P_new(0, 300));

  LL *f = LL_new(64);
  LL *parts[] = {olines, inlines, lip0s, lip1s, ef, sh, sh2, bbs, teeth0s, teeth1s, jaw};
  for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) LL_push_all(f, parts[i]);
  Part r = {outlinel, f};
  return r;
}

// ---------------------------------------------------------------------------
// The whole fish

static double bean(double x) {
  return fd_pow(0.25 - fd_pow(x - 0.5, 2), 0.5) * (2.6 + 2.4 * fd_pow(x, 1.5)) * 0.542;
}

static L *pair(P *a, P *b) { return L_of(2, a, b); }

static LL *fish(Arg *arg) {
  int n = 32;
  L *curve0 = L_new(n), *curve1 = L_new(n);
  if (arg->body_curve_type == 0) {
    double s = arg->body_curve_amount;
    for (int i = 0; i < n; i++) {
      double t = (double)i / (n - 1);
      double x = 225 + (t - 0.5) * arg->body_length;
      double y = 150 - (fd_sin(t * PI) * lerp(0.5, 1, noise2(t * 2, 1)) * s + (1 - s)) * arg->body_height;
      L_push(curve0, P_new(x, y));
    }
    for (int i = 0; i < n; i++) {
      double t = (double)i / (n - 1);
      double x = 225 + (t - 0.5) * arg->body_length;
      double y = 150 + (fd_sin(t * PI) * lerp(0.5, 1, noise2(t * 2, 2)) * s + (1 - s)) * arg->body_height;
      L_push(curve1, P_new(x, y));
    }
  } else if (arg->body_curve_type == 1) {
    for (int i = 0; i < n; i++) {
      double t = (double)i / (n - 1);
      double x = 225 + (t - 0.5) * arg->body_length;
      double y = 150 - lerp(1 - arg->body_curve_amount, 1, lerp(0, 1, noise2(t * 1.2, 1)) * bean(1 - t)) * arg->body_height;
      L_push(curve0, P_new(x, y));
    }
    for (int i = 0; i < n; i++) {
      double t = (double)i / (n - 1);
      double x = 225 + (t - 0.5) * arg->body_length;
      double y = 150 + lerp(1 - arg->body_curve_amount, 1, lerp(0, 1, noise2(t * 1.2, 2)) * bean(1 - t)) * arg->body_height;
      L_push(curve1, P_new(x, y));
    }
  }
  L *outline = L_concat(curve0, rev_copy(curve1));
  TRACE("start");
  LL *sh = shade_shape(outline, 8, -12, -12);
  TRACE("body shade");

  Pattern pat;
  memset(&pat, 0, sizeof pat);
  const Pattern *pf = NULL;
  if (arg->pattern_type == 1) {
    pattern_dot(&pat, arg->pattern_scale);
    pf = &pat;
  } else if (arg->pattern_type == 2) {
    pat.kind = 2;
    pf = &pat;
  } else if (arg->pattern_type == 3) {
    pat.kind = 3;
    pat.scale = arg->pattern_scale;
    pf = &pat;
  }

  LL *bd = NULL;
  if (arg->scale_type == 0) bd = fish_body_a(curve0, curve1, arg->scale_scale, pf);
  else if (arg->scale_type == 1) bd = fish_body_b(curve0, curve1, arg->scale_scale, pf);
  else if (arg->scale_type == 2) bd = fish_body_c(curve0, curve1, arg->scale_scale);
  else bd = fish_body_d(curve0, curve1, arg->scale_scale);

  TRACE("body scales");
  // dorsal fin
  double f0_a0, f0_a1, f0_cv;
  WFn f0_func = {F0_A, arg};
  if (arg->dorsal_type == 0) {
    f0_a0 = 0.2 + deviate(0.05);
    f0_a1 = 0.3 + deviate(0.05);
    f0_cv = 0;
  } else {
    f0_a0 = 0.6 + deviate(0.05);
    f0_a1 = 0.3 + deviate(0.05);
    f0_cv = arg->dorsal_length / 8;
    f0_func.kind = F0_B;
  }
  Part p0;
  if (arg->dorsal_texture_type == 0) {
    L *f0_curve = resample(L_slice(curve0, arg->dorsal_start, arg->dorsal_end), 5);
    p0 = fin_a(f0_curve, f0_a0, f0_a1, f0_func, 0, f0_cv, 0, 10);
  } else {
    L *f0_curve = resample(L_slice(curve0, arg->dorsal_start, arg->dorsal_end), 15);
    p0 = fin_b(f0_curve, f0_a0, f0_a1, f0_func, 1);
  }
  L *c0 = p0.c;
  LL *f0 = clip_out(p0.f, trsl_poly(outline, 0, 0.001));

  TRACE("dorsal");
  // pectoral fin ("wing")
  L *f1_curve = L_new(10);
  P *f1_pt = lerp2dp(curve0->a[(int)arg->wing_start], curve1->a[(int)arg->wing_end], arg->wing_y);
  for (int i = 0; i < 10; i++) {
    double t = i / 9.0;
    double y = lerp(f1_pt->y - arg->wing_width / 2, f1_pt->y + arg->wing_width / 2, t);
    L_push(f1_curve, P_new(f1_pt->x, y));
  }
  double f1_a0, f1_a1, f1_soft, f1_cv;
  WFn f1_func = {F1_A, arg};
  if (arg->wing_type == 0) {
    f1_a0 = -0.4 + deviate(0.05);
    f1_a1 = 0.4 + deviate(0.05);
    f1_soft = 10;
    f1_cv = 0;
  } else {
    f1_a0 = 0 + deviate(0.05);
    f1_a1 = 0.4 + deviate(0.05);
    f1_soft = 5;
    f1_cv = arg->wing_length / 25;
    f1_func.kind = F1_B;
  }
  Part p1;
  if (arg->wing_texture_type == 0) {
    f1_curve = resample(f1_curve, 1.5);
    p1 = fin_a(f1_curve, f1_a0, f1_a1, f1_func, 1, f1_cv, 0, f1_soft);
  } else {
    f1_curve = resample(f1_curve, 4);
    p1 = fin_b(f1_curve, f1_a0, f1_a1, f1_func, 0.3);
  }
  L *c1 = p1.c;
  LL *f1 = p1.f;
  bd = clip_out(bd, c1);

  TRACE("wing");
  // pelvic fin
  double f2_a0, f2_a1;
  WFn f2_func = {F2_A, arg};
  if (arg->pelvic_type == 0) {
    f2_a0 = -0.8 + deviate(0.05);
    f2_a1 = -0.5 + deviate(0.05);
  } else {
    f2_a0 = -0.9 + deviate(0.05);
    f2_a1 = -0.3 + deviate(0.05);
    f2_func.kind = F2_B;
  }
  Part p2;
  if (arg->pelvic_texture_type == 0) {
    L *f2_curve = resample(L_reverse(L_slice(curve1, arg->pelvic_start, arg->pelvic_end)), arg->pelvic_type ? 2 : 5);
    p2 = fin_a(f2_curve, f2_a0, f2_a1, f2_func, 0, 0, 0, 10);
  } else {
    L *f2_curve = resample(L_reverse(L_slice(curve1, arg->pelvic_start, arg->pelvic_end)), arg->pelvic_type ? 2 : 15);
    p2 = fin_b(f2_curve, f2_a0, f2_a1, f2_func, 1);
  }
  LL *f2 = clip_out(p2.f, c1);

  // anal fin
  double f3_a0, f3_a1;
  WFn f3_func = {F3_A, arg};
  f3_a0 = -0.4 + deviate(0.05);
  f3_a1 = -0.4 + deviate(0.05);
  if (arg->anal_type != 0) f3_func.kind = F3_B;
  Part p3;
  if (arg->anal_texture_type == 0) {
    L *f3_curve = resample(L_reverse(L_slice(curve1, arg->anal_start, arg->anal_end)), 5);
    p3 = fin_a(f3_curve, f3_a0, f3_a1, f3_func, 0, 0, 0, 10);
  } else {
    L *f3_curve = resample(L_reverse(L_slice(curve1, arg->anal_start, arg->anal_end)), 15);
    p3 = fin_b(f3_curve, f3_a0, f3_a1, f3_func, 1);
  }
  LL *f3 = clip_out(p3.f, c1);

  TRACE("pelvic+anal");
  // tail
  double f4_r = distp(curve0->a[curve0->n - 2], curve1->a[curve1->n - 2]);
  int f4_n = toi32(f4_r / 1.5);
  f4_n = (int)js_max(js_min(f4_n, 20), 8);
  double f4_d = f4_r / f4_n;
  P *e0 = curve0->a[curve0->n - 1], *e1 = curve1->a[curve1->n - 1];
  P *s0 = curve0->a[curve0->n - 2], *s1 = curve1->a[curve1->n - 2];
  WFn f4_func = {T0, arg};
  Part p4;
  int tt = (int)arg->tail_type;
  if (tt == 0) {
    p4 = fin_a(resample(pair(e0, e1), f4_d), -0.6, 0.6, f4_func, 1, 0, 0, 10);
  } else if (tt == 1) {
    f4_func.kind = T1;
    p4 = fin_a(resample(pair(s0, s1), f4_d), -0.6, 0.6, f4_func, 1, 0, 0, 10);
  } else if (tt == 2) {
    double cv = arg->tail_length / 8;
    f4_func.kind = T2;
    p4 = fin_a(resample(pair(e0, e1), f4_d * 0.7), -0.6, 0.6, f4_func, 1, cv, -cv, 10);
  } else {
    f4_func.kind = tt == 3 ? T3 : tt == 4 ? T4 : T5;
    p4 = fin_a(resample(pair(s0, s1), f4_d), -0.6, 0.6, f4_func, 1, 0, 0, 10);
  }
  bd = clip_out(bd, trsl_poly(p4.c, 1, 0));
  LL *f4 = clip_out(p4.f, c1);

  TRACE("tail");
  // finlets
  LL *f5 = LL_new(0);
  if (arg->finlet_type == 1) {
    L *f5_curve = resample(L_slice(curve0, arg->dorsal_end, -2), 5);
    f5 = finlet(f5_curve, 5, 1).f;
    f5_curve = resample(L_reverse(L_slice(curve1, arg->anal_end, -2)), 5);
    if (f5_curve->n > 1) f5 = finlet(f5_curve, 5, 1).f;
  } else if (arg->finlet_type == 2) {
    L *f5_curve = resample(L_slice(curve0, 27, 30), 5);
    Part p5 = fin_adipose(f5_curve, 20, -5, 6);
    f5 = p5.f;
    outline = poly_union(outline, trsl_poly(p5.c, 0, -1));
  } else if (arg->finlet_type == 3) {
    L *f5_curve = resample(L_slice(curve0, arg->dorsal_end + 2, -3), 5);
    if (f5_curve->n > 2) {
      WFn f5_func = {F5_3, arg};
      f5 = fin_a(f5_curve, 0.2, 0.3, f5_func, 0, 0, 0, 10).f;
    }
  }

  TRACE("finlets");
  // head
  Part ph;
  if (arg->neck_type == 0) {
    ph = fish_head(50 - arg->head_length, 150 + arg->nose_height, curve0->a[6]->x, curve0->a[6]->y, curve1->a[5]->x,
                   curve1->a[5]->y, arg);
  } else {
    ph = fish_head(50 - arg->head_length, 150 + arg->nose_height, curve0->a[5]->x, curve0->a[5]->y, curve1->a[6]->x,
                   curve1->a[6]->y, arg);
  }
  TRACE("head");
  L *cf = ph.c;
  LL *fh = ph.f;
  bd = clip_out(bd, cf);
  sh = clip_out(sh, cf);
  sh = clip_out(sh, c1);
  f1 = clip_out(f1, cf);
  f0 = clip_out(f0, c1);

  LL *sh2 = LL_new(0);
  if (pf) {
    if (arg->scale_type > 1) {
      sh2 = patternshade_shape(poly_union(outline, trsl_poly(c0, 0, 3)), 3.5, pf);
    } else {
      sh2 = patternshade_shape(c0, 4.5, pf);
    }
    sh2 = clip_out(sh2, cf);
    sh2 = clip_out(sh2, c1);
  }

  LL *sh3 = LL_new(0);
  if (arg->pattern_type == 4) {
    sh3 = smalldot_shape(poly_union(outline, trsl_poly(c0, 0, 5)), arg->pattern_scale);
    // The middle pass's lists are throwaway: build them in STAGE.
    Mark stage = mark_of(A_STAGE);
    list_arena = A_STAGE;
    LL *mid = clip_out(sh3, c1);
    list_arena = A_MAIN;
    sh3 = clip_out(mid, cf);
    reset_to(stage);
  }

  TRACE("patterns+clip");
  LL *all = LL_new(1024);
  LL *parts[] = {bd, f0, f1, f2, f3, f4, f5, fh, sh, sh2, sh3};
  for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) LL_push_all(all, parts[i]);
  return all;
}

// ---------------------------------------------------------------------------
// Framing and cleanup

static LL *reframe(LL *polylines, double pad) {
  double W = 500 - pad * 2;
  double H = (300 - pad * 2) - 0; // no label
  double xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
  for (int i = 0; i < polylines->n; i++) {
    for (int j = 0; j < polylines->a[i]->n; j++) {
      const P *p = polylines->a[i]->a[j];
      xmin = js_min(xmin, p->x);
      ymin = js_min(ymin, p->y);
      xmax = js_max(xmax, p->x);
      ymax = js_max(ymax, p->y);
    }
  }
  BBox bbox = {xmin, ymin, xmax - xmin, ymax - ymin};
  double sw = W / bbox.w, sh = H / bbox.h;
  double s = js_min(sw, sh);
  double px = (W - bbox.w * s) / 2, py = (H - bbox.h * s) / 2;
  for (int i = 0; i < polylines->n; i++) {
    L *l = polylines->a[i];
    for (int j = 0; j < l->n; j++) {
      double x = l->a[j]->x, y = l->a[j]->y;
      x = (x - bbox.x) * s + px + pad;
      y = (y - bbox.y) * s + py + pad;
      l->a[j] = P_new(x, y);
    }
  }
  return polylines;
}

static LL *cleanup(LL *polylines) {
  for (int i = polylines->n - 1; i >= 0; i--) {
    polylines->a[i] = approx_poly_dp(polylines->a[i], 0.1);
    L *l = polylines->a[i];
    for (int j = 0; j < l->n; j++) {
      l->a[j]->x = toi32(l->a[j]->x * 10000) / 10000.0;
      l->a[j]->y = toi32(l->a[j]->y * 10000) / 10000.0;
    }
    if (l->n < 2) {
      memmove(polylines->a + i, polylines->a + i + 1, sizeof(L *) * (polylines->n - i - 1));
      polylines->n--;
      continue;
    }
    // The JS also removes 2-point lines when dist(...polylines[0],
    // ...polylines[1]) < 0.9, but that spreads polylines rather than points,
    // so the distance is always NaN and nothing is ever removed.
  }
  return polylines;
}

// ---------------------------------------------------------------------------
// Parameters from the seed

static double rndtri(double a, double b, double c) {
  double s0 = (b - a) / 2, s1 = (c - b) / 2, s = s0 + s1;
  double r = rnd() * s;
  if (r < s0) return a + sqrt(2 * r * (b - a));
  return c - sqrt(2 * (s - r) * (c - b));
}

// choice(opts) with equal weights.
static double choice(int n, const double *opts) {
  double r = rnd() * n, s = 0;
  for (int i = 0; i < n; i++) {
    s += 1;
    if (r <= s) return opts[i];
  }
  return NAN; // undefined in the JS; unreachable since rand() <= 1
}
#define CHOICE(...) choice(sizeof((double[]){__VA_ARGS__}) / sizeof(double), (double[]){__VA_ARGS__})

static Arg generate_params(void) {
  Arg a = {
      .body_curve_type = 0, .body_curve_amount = 0.85, .body_length = 350, .body_height = 90, .scale_type = 1,
      .scale_scale = 1, .pattern_type = 3, .pattern_scale = 1, .dorsal_texture_type = 1, .dorsal_type = 0,
      .dorsal_length = 100, .dorsal_start = 8, .dorsal_end = 27, .wing_texture_type = 0, .wing_type = 0,
      .wing_start = 6, .wing_end = 6, .wing_y = 0.7, .wing_length = 130, .wing_width = 10, .pelvic_start = 9,
      .pelvic_end = 14, .pelvic_length = 85, .pelvic_type = 0, .pelvic_texture_type = 0, .anal_start = 19,
      .anal_end = 29, .anal_length = 50, .anal_type = 0, .anal_texture_type = 0, .tail_type = 0, .tail_length = 75,
      .finlet_type = 0, .neck_type = 0, .nose_height = 0, .mouth_size = 8, .head_length = 30,
      .head_texture_amount = 60, .has_moustache = 1, .moustache_length = 10, .has_beard = 0, .has_teeth = 1,
      .teeth_length = 8, .teeth_space = 3.5, .beard_length = 30, .eye_type = 1, .eye_size = 10, .jaw_size = 1,
      .jaw_open = 1,
  };
  a.body_curve_type = CHOICE(0, 1);
  a.body_curve_amount = rndtri(0.5, 0.85, 0.98);
  a.body_length = rndtri(200, 350, 420);
  a.body_height = rndtri(45, 90, 150);
  a.scale_type = CHOICE(0, 1, 2, 3);
  a.scale_scale = rndtri(0.8, 1, 1.5);
  a.pattern_type = CHOICE(0, 1, 2, 3, 4);
  a.pattern_scale = rndtri(0.5, 1, 2);
  a.dorsal_texture_type = CHOICE(0, 1);
  a.dorsal_type = CHOICE(0, 1);
  a.dorsal_length = rndtri(30, 90, 180);
  if (a.dorsal_type == 0) {
    a.dorsal_start = toi32(rndtri(7, 8, 15));
    a.dorsal_end = toi32(rndtri(20, 27, 28));
  } else {
    a.dorsal_start = toi32(rndtri(11, 12, 16));
    a.dorsal_end = toi32(rndtri(19, 21, 24));
  }
  a.wing_texture_type = CHOICE(0, 1);
  a.wing_type = CHOICE(0, 1);
  if (a.wing_type == 0) a.wing_length = rndtri(40, 130, 200);
  else a.wing_length = rndtri(40, 150, 350);
  if (a.wing_texture_type == 0) {
    a.wing_width = rndtri(7, 10, 20);
    a.wing_y = rndtri(0.45, 0.7, 0.85);
  } else {
    a.wing_width = rndtri(20, 30, 50);
    a.wing_y = rndtri(0.45, 0.65, 0.75);
  }
  a.wing_start = toi32(rndtri(5, 6, 8));
  a.wing_end = toi32(rndtri(5, 6, 8));

  a.pelvic_texture_type = a.dorsal_texture_type ? CHOICE(0, 1) : 0;
  a.pelvic_type = CHOICE(0, 1);
  a.pelvic_length = rndtri(30, 85, 140);
  if (a.pelvic_type == 0) {
    a.pelvic_start = toi32(rndtri(7, 9, 11));
    a.pelvic_end = toi32(rndtri(13, 14, 15));
  } else {
    a.pelvic_start = toi32(rndtri(7, 9, 12));
    a.pelvic_end = a.pelvic_start + 2;
  }

  a.anal_texture_type = a.dorsal_texture_type ? CHOICE(0, 1) : 0;
  a.anal_type = CHOICE(0, 1);
  a.anal_length = rndtri(20, 50, 80);
  a.anal_start = toi32(rndtri(16, 19, 23));
  a.anal_end = toi32(rndtri(25, 29, 31));

  a.tail_type = CHOICE(0, 1, 2, 3, 4, 5);
  a.tail_length = rndtri(50, 75, 180);

  a.finlet_type = CHOICE(0, 1, 2, 3);

  a.neck_type = CHOICE(0, 1);
  a.nose_height = rndtri(-50, 0, 35);
  a.head_length = rndtri(20, 30, 35);
  a.mouth_size = toi32(rndtri(6, 8, 11));

  a.head_texture_amount = toi32(rndtri(30, 60, 160));
  a.has_moustache = CHOICE(0, 0, 0, 1);
  a.has_beard = CHOICE(0, 0, 0, 0, 0, 1);
  a.moustache_length = toi32(rndtri(10, 20, 40));
  a.beard_length = toi32(rndtri(20, 30, 50));

  a.eye_type = CHOICE(0, 1);
  a.eye_size = rndtri(8, 10, 28);

  a.jaw_size = rndtri(0.7, 1, 1.4);

  a.has_teeth = CHOICE(0, 1, 1);
  a.teeth_length = rndtri(5, 8, 15);
  a.teeth_space = rndtri(3, 3.5, 6);
  return a;
}

// str_to_seed over the name's UTF-16 code units, as charCodeAt sees them.
static double str_to_seed(const char *s) {
  double n = 1;
  int i = 0;
  const unsigned char *u = (const unsigned char *)s;
  while (*u) {
    uint32_t cp;
    if (*u < 0x80) cp = *u++;
    else if ((*u & 0xE0) == 0xC0 && u[1]) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
    else if ((*u & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
    else if ((*u & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
      cp = ((uint32_t)(u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
      u += 4;
    } else { cp = 0xFFFD; u++; }
    uint32_t units[2];
    int nu = 0;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      units[nu++] = 0xD800 + (cp >> 10);
      units[nu++] = 0xDC00 + (cp & 0x3FF);
    } else {
      units[nu++] = cp;
    }
    for (int k = 0; k < nu; k++, i++) {
      int32_t x = (int32_t)units[k] + 1;
      int32_t v = toi32(n);
      v ^= shl(x, 7 + (i % 5));
      v ^= shl(v, 17);
      v ^= sar(v, 13);
      v ^= shl(v, 5);
      n = fmod((double)(uint32_t)v, 4294967295.0);
    }
  }
  return n;
}

// ---------------------------------------------------------------------------

int fishdraw(const char *name, fd_drawing *out) {
  memset(out, 0, sizeof *out);
  if (setjmp(fail_jmp)) {
    arena_release();
    return 1;
  }
  perlin_ready = 0;
  peak_bytes = 0;
  jsr = (int32_t)(uint32_t)str_to_seed(name);
  Arg arg = generate_params();
#ifdef FISHDRAW_TRACE
  fprintf(stderr, "scale_type %g pattern %g finlet %g\n", arg.scale_type, arg.pattern_type, arg.finlet_type);
#endif
  LL *drawing = cleanup(reframe(fish(&arg), 20));
  TRACE("framed");

  size_t npts = 0;
  for (int i = 0; i < drawing->n; i++) npts += drawing->a[i]->n;
  out->lines = malloc(sizeof(fd_polyline) * (size_t)(drawing->n > 0 ? drawing->n : 1));
  fd_point *pts = malloc(sizeof(fd_point) * (npts ? npts : 1));
  if (!out->lines || !pts) {
    free(out->lines);
    free(pts);
    out->lines = NULL;
    arena_release();
    return 1;
  }
  for (int i = 0; i < drawing->n; i++) {
    out->lines[i].points = pts;
    out->lines[i].n = drawing->a[i]->n;
    for (int j = 0; j < drawing->a[i]->n; j++) {
      pts->x = drawing->a[i]->a[j]->x;
      pts->y = drawing->a[i]->a[j]->y;
      pts++;
    }
  }
  out->n = drawing->n;
  out->peak_bytes = peak_bytes;
  arena_release();
  return 0;
}

void fishdraw_free(fd_drawing *d) {
  if (d->lines) free(d->n ? d->lines[0].points : NULL);
  free(d->lines);
  d->lines = NULL;
  d->n = 0;
}
