// Host test for esp32/main/forecast.c, run by test/esp32.test.js.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../main/forecast.h"

// What Open-Meteo sends back (the shape of it, with "daily_units" first).
static const char *REPLY =
    "{\"latitude\":51.46,\"longitude\":-2.6,\"generationtime_ms\":0.03,\"utc_offset_seconds\":3600,"
    "\"timezone\":\"Europe/London\",\"timezone_abbreviation\":\"GMT+1\",\"elevation\":24.0,"
    "\"daily_units\":{\"time\":\"iso8601\",\"weather_code\":\"wmo code\",\"temperature_2m_max\":\"\xC2\xB0"
    "C\",\"temperature_2m_min\":\"\xC2\xB0"
    "C\"},"
    "\"daily\":{\"time\":[\"2026-10-03\"],\"weather_code\":[61],\"temperature_2m_max\":[14.2],"
    "\"temperature_2m_min\":[-8.1]}}";

static const char *IPINFO = "{\n  \"ip\": \"203.0.113.7\",\n  \"city\": \"Bristol\",\n  \"country\": \"GB\",\n"
                            "  \"loc\": \"51.4552,-2.5966\",\n  \"timezone\": \"Europe/London\"\n}";

int main(void) {
  forecast f;
  assert(forecast_parse(REPLY, &f) == 0);
  assert(!strcmp(f.date, "2026-10-03"));
  assert(f.code == 61 && f.high == 14.2 && f.low == -8.1 && f.utc_offset == 3600);

  // West of Greenwich, pretty-printed.
  assert(forecast_parse("{ \"utc_offset_seconds\" : -25200, \"daily\": { \"time\": [ \"2026-01-31\" ], "
                        "\"weather_code\": [ 3 ], \"temperature_2m_max\": [ 44.6 ], \"temperature_2m_min\": [ 30 ] } }",
                        &f) == 0);
  assert(f.utc_offset == -25200 && f.code == 3 && f.high == 44.6 && f.low == 30 && !strcmp(f.date, "2026-01-31"));

  // Missing values aren't a forecast.
  assert(forecast_parse("{\"utc_offset_seconds\":0,\"daily\":{\"time\":[\"2026-10-03\"],\"weather_code\":[null],"
                        "\"temperature_2m_max\":[1],\"temperature_2m_min\":[0]}}",
                        &f) == -1);
  assert(forecast_parse("{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90\"}", &f) == -1);
  assert(forecast_parse("", &f) == -1);

  double lat, lon;
  assert(ipinfo_parse(IPINFO, &lat, &lon) == 0 && lat == 51.4552 && lon == -2.5966);
  assert(ipinfo_parse("{\"ip\":\"10.0.0.1\",\"bogon\":true}", &lat, &lon) == -1);

  char url[256];
  forecast_url(url, sizeof url, 51.4552, -2.5966, 1);
  assert(!strcmp(url, "https://api.open-meteo.com/v1/forecast?latitude=51.4552&longitude=-2.5966"
                      "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
                      "&temperature_unit=fahrenheit"));
  puts("ok");
  return 0;
}
