// Render a fish-fodder frame from the command line, as a PBM image.
//   frame_cli --time 2026-10-03T14:05 [--size 800x480] [--rotate 90] [--12h]
//             [--hourly] [--salt ID] [--weather rain,13,8]
//             [--name NAME --note NOTE [--rare]] [-o frame.pbm]
// With --catch it prints the hour's catch instead of drawing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fishfodder.h"

static const char *const KINDS[] = {"sun", "partly", "cloud", "fog", "drizzle", "rain", "snow", "storm"};

static int weekday(int y, int m, int d) { // Sakamoto's method, 0 = Sunday
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void json_string(const char *s) {
  putchar('"');
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') putchar('\\');
    putchar(*s);
  }
  putchar('"');
}

static int usage(void) {
  fprintf(stderr, "usage: frame_cli --time YYYY-MM-DDTHH:MM [--size WxH] [--rotate DEG] [--12h] [--hourly]\n"
                  "                 [--salt ID] [--weather KIND,HIGH,LOW] [--name NAME --note NOTE [--rare]]\n"
                  "                 [--catch] [-o FILE]\n");
  return 2;
}

int main(int argc, char **argv) {
  ff_time t = {0};
  ff_options o = {800, 480, 0, 0, 0, NULL, NULL, NULL};
  ff_weather w;
  ff_catch over = {"", "", "", 0};
  int have_time = 0, have_over = 0, print_catch = 0;
  const char *out = NULL;
  for (int a = 1; a < argc; a++) {
    const char *arg = argv[a], *val = a + 1 < argc ? argv[a + 1] : NULL;
    if (!strcmp(arg, "--12h")) o.clock_12h = 1;
    else if (!strcmp(arg, "--hourly")) o.hourly = 1;
    else if (!strcmp(arg, "--rare")) over.rare = 1, have_over = 1;
    else if (!strcmp(arg, "--catch")) print_catch = 1;
    else if (!val) return usage();
    else {
      a++;
      if (!strcmp(arg, "--time")) {
        if (sscanf(val, "%d-%d-%dT%d:%d", &t.year, &t.month, &t.day, &t.hour, &t.minute) != 5) return usage();
        t.weekday = weekday(t.year, t.month, t.day);
        have_time = 1;
      } else if (!strcmp(arg, "--size")) {
        if (sscanf(val, "%dx%d", &o.width, &o.height) != 2) return usage();
      } else if (!strcmp(arg, "--rotate")) o.rotate = atoi(val);
      else if (!strcmp(arg, "--salt")) o.salt = val;
      else if (!strcmp(arg, "--weather")) {
        char kind[16];
        if (sscanf(val, "%15[a-z],%lf,%lf", kind, &w.high, &w.low) != 3) return usage();
        w.kind = FF_CLOUD;
        for (int k = 0; k < 8; k++)
          if (!strcmp(kind, KINDS[k])) w.kind = (ff_sky)k;
        o.weather = &w;
      } else if (!strcmp(arg, "--name")) snprintf(over.name, sizeof over.name, "%s", val), have_over = 1;
      else if (!strcmp(arg, "--note")) snprintf(over.note, sizeof over.note, "%s", val), have_over = 1;
      else if (!strcmp(arg, "-o")) out = val;
      else return usage();
    }
  }
  if (!have_time) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    t = (ff_time){lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_wday};
  }
  if (have_over) o.catch_override = &over;

  if (print_catch) {
    ff_catch c;
    ff_catch_for(&t, o.salt, &c);
    printf("{\"key\":");
    json_string(c.key);
    printf(",\"name\":");
    json_string(c.name);
    printf(",\"note\":");
    json_string(c.note);
    printf(",\"rare\":%s}\n", c.rare ? "true" : "false");
    return 0;
  }

  ff_bitmap b;
  ff_catch c;
  if (ff_render(&t, &o, &b, &c)) {
    fprintf(stderr, "frame_cli: drawing failed\n");
    return 1;
  }
  FILE *f = out ? fopen(out, "wb") : stdout;
  if (!f) {
    perror(out);
    return 1;
  }
  // PBM (P4): 1 bit per pixel, 1 = black, rows padded to whole bytes.
  fprintf(f, "P4\n%d %d\n", b.width, b.height);
  int stride = (b.width + 7) / 8;
  unsigned char *row = malloc(stride);
  for (int y = 0; y < b.height; y++) {
    memset(row, 0, stride);
    for (int x = 0; x < b.width; x++)
      if (b.pixels[y * b.width + x]) row[x >> 3] |= 0x80 >> (x & 7);
    fwrite(row, 1, stride, f);
  }
  free(row);
  if (out) fclose(f);
  fprintf(stderr, "%s: %s\n", c.key, c.name);
  ff_bitmap_free(&b);
  ff_forget_fish();
  return 0;
}
