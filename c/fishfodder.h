// fish-fodder's clock face in C: the same frames as src/render.js, pixel for
// pixel, for devices that can't run JavaScript (an ESP32 frame, say).
#ifndef FISHFODDER_H
#define FISHFODDER_H

#include <stdint.h>

// Local time, as the frame shows it.
typedef struct {
  int year, month, day; // month 1..12
  int hour, minute;
  int weekday; // 0 = Sunday
} ff_time;

typedef enum { FF_SUN, FF_PARTLY, FF_CLOUD, FF_FOG, FF_DRIZZLE, FF_RAIN, FF_SNOW, FF_STORM } ff_sky;

typedef struct {
  ff_sky kind;
  double high, low; // in whatever unit should be shown
} ff_weather;

typedef struct {
  char key[32];  // the hour it belongs to, "2026-10-03T14"
  char name[64];
  char note[112];
  int rare;
} ff_catch;

typedef struct {
  int width, height; // the panel, as mounted before rotation (800 x 480)
  int rotate;        // 0, 90, 180 or 270 degrees clockwise
  int clock_12h;     // 12-hour clock with am/pm
  int hourly;        // show HH:00 (for panels that only refresh hourly)
  const char *salt;  // frame ID: a different fish schedule; NULL or "" for none
  const ff_weather *weather; // NULL: no weather
  const ff_catch *catch_override; // NULL: the scheduled catch
} ff_options;

// The bitmap: width x height, one byte per pixel, 1 = ink.
typedef struct {
  int width, height;
  uint8_t *pixels;
} ff_bitmap;

// Which fish is on duty at `t` (and the hour key it was seeded from).
void ff_catch_for(const ff_time *t, const char *salt, ff_catch *out);

// Draw the frame for `t`. `out->pixels` is allocated (free with
// ff_bitmap_free). Returns 0, or non-zero if drawing the fish failed.
// The last fish drawn is cached, so redrawing within the hour is quick.
int ff_render(const ff_time *t, const ff_options *opts, ff_bitmap *out, ff_catch *catch_out);

void ff_bitmap_free(ff_bitmap *b);

// The sky for an Open-Meteo / WMO weather code.
ff_sky ff_sky_for_code(int wmo_code);

// Drop the cached fish (frees its memory).
void ff_forget_fish(void);

#endif
