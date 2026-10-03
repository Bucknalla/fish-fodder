// Just enough JSON reading for two known replies: find a key and read the
// number (or first array element, or string) after it.

#include "forecast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORECAST_URL "https://api.open-meteo.com/v1/forecast"

int forecast_url(char *buf, size_t size, double lat, double lon, int fahrenheit) {
  return snprintf(buf, size,
                  FORECAST_URL "?latitude=%.4f&longitude=%.4f"
                               "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                               "&timezone=auto&forecast_days=1%s",
                  lat, lon, fahrenheit ? "&temperature_unit=fahrenheit" : "");
}

static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

// The value of "key": just past the colon, or past `open` (such as '[')
// if given. Occurrences whose value doesn't start with `open` are skipped:
// Open-Meteo's "daily_units" repeats the keys with string values.
static const char *value_of(const char *json, const char *key, char open) {
  char pat[64];
  int n = snprintf(pat, sizeof pat, "\"%s\"", key);
  if (n < 0 || n >= (int)sizeof pat) return NULL;
  for (const char *p = json; (p = strstr(p, pat)); p += n) {
    const char *q = skip_ws(p + n);
    if (*q != ':') continue;
    q = skip_ws(q + 1);
    if (open) {
      if (*q != open) continue;
      q = skip_ws(q + 1);
    }
    return q;
  }
  return NULL;
}

static int number(const char *json, const char *key, char open, double *out) {
  const char *p = value_of(json, key, open);
  if (!p) return -1;
  char *end;
  double v = strtod(p, &end);
  if (end == p) return -1; // null, or not a number
  *out = v;
  return 0;
}

int forecast_parse(const char *json, forecast *out) {
  double code, offset;
  if (number(json, "weather_code", '[', &code) || number(json, "temperature_2m_max", '[', &out->high) ||
      number(json, "temperature_2m_min", '[', &out->low) || number(json, "utc_offset_seconds", 0, &offset))
    return -1;
  const char *d = value_of(json, "time", '[');
  if (!d || *d != '"' || strlen(d) < 12 || d[11] != '"') return -1;
  memcpy(out->date, d + 1, 10);
  out->date[10] = 0;
  out->code = (int)code;
  out->utc_offset = (int)offset;
  return 0;
}

int ipinfo_parse(const char *json, double *lat, double *lon) {
  const char *p = value_of(json, "loc", '"');
  if (!p) return -1;
  char *end;
  double a = strtod(p, &end);
  if (end == p || *end != ',') return -1;
  p = end + 1;
  double b = strtod(p, &end);
  if (end == p || *end != '"') return -1;
  *lat = a;
  *lon = b;
  return 0;
}
