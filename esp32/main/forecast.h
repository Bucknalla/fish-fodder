// Today's forecast from Open-Meteo, and a location from ipinfo.io: building
// the requests and reading the replies. Plain C, so it's tested on the host
// (test/esp32.test.js).
#ifndef FORECAST_H
#define FORECAST_H

#include <stddef.h>

typedef struct {
  char date[11];  // the local date it's for, "2026-10-03"
  int code;       // WMO weather code
  double high, low;
  int utc_offset; // seconds east of UTC, for the forecast's place and date
} forecast;

// The forecast request for (lat, lon). Returns snprintf's result.
int forecast_url(char *buf, size_t size, double lat, double lon, int fahrenheit);

// Read Open-Meteo's reply. Returns 0, or -1 if anything is missing.
int forecast_parse(const char *json, forecast *out);

#define IPINFO_URL "https://ipinfo.io/json"

// Read ipinfo.io's "loc": "51.4552,-2.5966". Returns 0, or -1.
int ipinfo_parse(const char *json, double *lat, double *lon);

#endif
