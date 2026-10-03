// Draw fish from the command line: one JSON array of polylines per name.
//   fishdraw_cli "Biggus fishus" ["another name" ...]
//   fishdraw_cli --stats NAME...   (time and working memory instead)
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fishdraw.h"

int main(int argc, char **argv) {
  int stats = argc > 1 && !strcmp(argv[1], "--stats");
  for (int a = 1 + stats; a < argc; a++) {
    fd_drawing d;
    clock_t t0 = clock();
    if (fishdraw(argv[a], &d)) {
      printf("ERROR\n");
      continue;
    }
    if (stats) {
      size_t pts = 0;
      for (int i = 0; i < d.n; i++) pts += d.lines[i].n;
      printf("%-34s %4d lines %6zu points  %7.1f ms  %6.1f MB\n", argv[a], d.n, pts,
             (clock() - t0) * 1000.0 / CLOCKS_PER_SEC, d.peak_bytes / 1048576.0);
    } else {
      putchar('[');
      for (int i = 0; i < d.n; i++) {
        printf(i ? ",[" : "[");
        for (int j = 0; j < d.lines[i].n; j++)
          printf(j ? ",[%.17g,%.17g]" : "[%.17g,%.17g]", d.lines[i].points[j].x, d.lines[i].points[j].y);
        putchar(']');
      }
      printf("]\n");
    }
    fishdraw_free(&d);
  }
  return 0;
}
