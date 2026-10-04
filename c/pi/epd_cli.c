// Drive an e-paper panel on a Raspberry Pi's header from the command line.
//
//   epd info                       which panel is fitted
//   epd show FRAME.pbm [--partial] [--awake]
//   epd sleep
//
// --panel waveshare-7in5-v2|inky-ac073tc1a|inky-e673 overrides detection.
// After `show` the panel sleeps, unless --awake: then a later
// `show --partial` can update it without a full refresh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd_linux.h"

static int usage(void) {
  fprintf(stderr, "usage: epd [--panel NAME] info | show FRAME.pbm [--partial] [--awake] | sleep\n");
  return 2;
}

static uint8_t *read_pbm(const char *path, int w, int h) {
  FILE *f = fopen(path, "rb");
  int pw, ph;
  if (!f) {
    perror(path);
    return NULL;
  }
  if (fscanf(f, "P4 %d %d", &pw, &ph) != 2 || fgetc(f) == EOF) {
    fprintf(stderr, "epd: %s isn't a PBM (P4) image\n", path);
    fclose(f);
    return NULL;
  }
  if (pw != w || ph != h) {
    fprintf(stderr, "epd: %s is %dx%d; the panel is %dx%d\n", path, pw, ph, w, h);
    fclose(f);
    return NULL;
  }
  size_t n = (size_t)(w + 7) / 8 * h;
  uint8_t *frame = malloc(n);
  if (!frame || fread(frame, 1, n, f) != n) {
    fprintf(stderr, "epd: %s is cut short\n", path);
    free(frame);
    fclose(f);
    return NULL;
  }
  fclose(f);
  for (size_t i = 0; i < n; i++) frame[i] = ~frame[i]; // PBM: set = black
  return frame;
}

int main(int argc, char **argv) {
  const char *panel = "auto", *cmd = NULL, *file = NULL;
  int partial = 0, awake = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--panel") && i + 1 < argc) panel = argv[++i];
    else if (!strcmp(argv[i], "--partial")) partial = 1;
    else if (!strcmp(argv[i], "--awake")) awake = 1;
    else if (!cmd) cmd = argv[i];
    else if (!file) file = argv[i];
    else return usage();
  }
  if (!cmd) return usage();

  const epd_driver *driver;
  const epd_pins *pins;
  char why[200] = "";
  if (!strcmp(panel, "auto")) {
    driver = epd_detect(&pins, why, sizeof why);
  } else if (!strcmp(panel, "waveshare-7in5-v2")) {
    driver = &epd_waveshare_7in5_v2, pins = &EPD_PINS_WAVESHARE;
  } else if (!strcmp(panel, "inky-ac073tc1a")) {
    driver = &epd_inky_ac073tc1a, pins = &EPD_PINS_INKY;
  } else if (!strcmp(panel, "inky-e673")) {
    driver = &epd_inky_e673, pins = &EPD_PINS_INKY;
  } else {
    fprintf(stderr, "epd: unknown panel %s\n", panel);
    return 2;
  }
  if (!driver) {
    fprintf(stderr, "epd: %s\n", why);
    return 1;
  }

  if (!strcmp(cmd, "info")) {
    inky_eeprom e;
    if (inky_read_eeprom(1, &e) == 0) {
      const char *name = epd_inky_variant_name(e.display_variant);
      printf("Inky EEPROM: %dx%d, display variant %d (%s), PCB %d.%d, written %s\n", e.width, e.height,
             e.display_variant, name ? name : "unknown", e.pcb_variant / 10, e.pcb_variant % 10, e.written);
    } else {
      printf("No Inky EEPROM (or I2C is off)\n");
    }
    printf("Panel: %s, %dx%d%s\n", driver->name, driver->width, driver->height,
           driver->partial ? ", partial refresh" : "");
    return 0;
  }

  uint8_t *frame = NULL;
  if (!strcmp(cmd, "show")) {
    if (!file) return usage();
    if (!(frame = read_pbm(file, driver->width, driver->height))) return 1;
  } else if (strcmp(cmd, "sleep")) {
    return usage();
  }

  epd_linux dev;
  if (epd_linux_open(&dev, driver, pins)) {
    fprintf(stderr, "epd: %s\n", dev.error);
    free(frame);
    return 1;
  }
  if (frame) {
    epd_show(&dev.panel, frame, partial && driver->partial);
    if (!awake) epd_sleep(&dev.panel);
  } else {
    dev.panel.mode = EPD_AWAKE; // whatever it's doing, put it to sleep
    epd_sleep(&dev.panel);
  }
  epd_linux_close(&dev);
  free(frame);
  return 0;
}
