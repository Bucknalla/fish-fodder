// Record what a driver does, for test/epd-parity.test.js to compare with the
// vendor's driver doing the same:
//   epd_trace waveshare-7in5-v2|inky-ac073tc1a|inky-e673 FRAME.pbm...
// Each frame is shown (the first in full, later ones as partial refreshes
// where the panel has them), then the panel is put to sleep. BUSY always
// reads as ready. Consecutive SPI writes are merged; long ones are printed
// as a length and FNV-1a hash.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd.h"

static const char *const PINS[] = {"RST", "DC", "CS", "PWR"};
static uint8_t *pending;
static size_t npending, cap;

static void flush(void) {
  if (!npending) return;
  printf("spi n=%zu ", npending);
  if (npending <= 32) {
    for (size_t i = 0; i < npending; i++) printf("%02x", pending[i]);
  } else {
    unsigned long long h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < npending; i++) h = (h ^ pending[i]) * 0x100000001b3ull;
    printf("#%016llx", h);
  }
  putchar('\n');
  npending = 0;
}

static void spi(void *ctx, const uint8_t *d, size_t n) {
  (void)ctx;
  if (npending + n > cap) {
    cap = (npending + n) * 2;
    pending = realloc(pending, cap);
  }
  memcpy(pending + npending, d, n);
  npending += n;
}

static void pin(void *ctx, int p, int v) {
  (void)ctx;
  flush();
  printf("pin %s %d\n", PINS[p], v);
}

static int busy(void *ctx) {
  (void)ctx;
  return 1;
}

static void delay_ms(void *ctx, int ms) {
  (void)ctx;
  flush();
  printf("delay %d\n", ms);
}

// A PBM (P4) as a frame: PBM sets bits for black, frames for white.
static uint8_t *read_pbm(const char *path, int w, int h) {
  FILE *f = fopen(path, "rb");
  int pw, ph;
  if (!f || fscanf(f, "P4 %d %d", &pw, &ph) != 2 || fgetc(f) == EOF || pw != w || ph != h) {
    fprintf(stderr, "epd_trace: %s isn't a %dx%d PBM\n", path, w, h);
    exit(1);
  }
  size_t n = (size_t)(w + 7) / 8 * h;
  uint8_t *frame = malloc(n);
  if (fread(frame, 1, n, f) != n) exit(1);
  fclose(f);
  for (size_t i = 0; i < n; i++) frame[i] = ~frame[i];
  return frame;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: epd_trace PANEL FRAME.pbm...\n");
    return 2;
  }
  const epd_driver *d = !strcmp(argv[1], "waveshare-7in5-v2") ? &epd_waveshare_7in5_v2
                        : !strcmp(argv[1], "inky-ac073tc1a") ? &epd_inky_ac073tc1a
                        : !strcmp(argv[1], "inky-e673")      ? &epd_inky_e673
                                                             : NULL;
  if (!d) {
    fprintf(stderr, "epd_trace: unknown panel %s\n", argv[1]);
    return 2;
  }
  epd_io io = {NULL, spi, pin, busy, delay_ms};
  epd e = {d, &io, 0};
  for (int i = 2; i < argc; i++) {
    uint8_t *frame = read_pbm(argv[i], d->width, d->height);
    epd_show(&e, frame, i > 2 && d->partial);
    free(frame);
  }
  epd_sleep(&e);
  flush();
  return 0;
}
