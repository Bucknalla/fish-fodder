// fishdraw in C: a port of src/vendor/fishdraw.js (Lingdong Huang's
// procedural fish, MIT licence) that draws exactly the same fish for the
// same name, down to the last bit of every coordinate.
#ifndef FISHDRAW_H
#define FISHDRAW_H

#include <stddef.h>

typedef struct {
  double x, y;
} fd_point;

typedef struct {
  fd_point *points;
  int n;
} fd_polyline;

typedef struct {
  fd_polyline *lines;
  int n;
  size_t peak_bytes; // working memory the drawing needed
} fd_drawing;

// Draw the fish for `name` (UTF-8), framed in fishdraw's 500x300 space with
// no label, exactly like draw_fish() in the JavaScript.
// Returns 0 on success; non-zero if the JavaScript would have thrown or
// memory ran out (out is then empty).
int fishdraw(const char *name, fd_drawing *out);

void fishdraw_free(fd_drawing *d);

// Hooks for small devices: where working memory comes from. Defaults are
// malloc and free. The arena asks for blocks of at least 64 KB.
void fishdraw_set_allocator(void *(*alloc)(size_t), void (*release)(void *));

#endif
