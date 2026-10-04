// fish-fodder on an ESP32-S3: the clock, today's weather and the hour's fish
// on an e-paper panel (Waveshare 7.5" V2 or Inky Impression 7.3"). The frame comes from the C port in c/,
// identical to what the Node app and simulator draw.
//
// Two ways to run (menuconfig, "Fish Fodder"):
// - every minute: stays awake on USB power, partial refresh each minute and
//   a full refresh when the new fish arrives on the hour (panels without
//   partial refresh, like the Inkys, update hourly instead);
// - hourly: wakes on the hour, shows HH:00 and the new fish, deep-sleeps.

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fishdraw.h"
#include "fishfodder.h"
#include "forecast.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs_flash.h"
#include "panel.h"
#include "sdkconfig.h"

static const char *TAG = "fish-fodder";

#ifdef CONFIG_FF_UPDATE_HOURLY
#define HOURLY 1
#else
#define HOURLY 0
#endif
#ifdef CONFIG_FF_CLOCK_12H
#define CLOCK_12H 1
#else
#define CLOCK_12H 0
#endif
#ifdef CONFIG_FF_FAHRENHEIT
#define FAHRENHEIT 1
#else
#define FAHRENHEIT 0
#endif

// Kept in RTC memory, so it survives deep sleep between hourly updates.
static RTC_DATA_ATTR struct {
  int have_place;
  double lat, lon;
  int have_offset;
  int utc_offset; // from the forecast, when no time zone is configured
  int have_forecast;
  forecast fc;
} st;

static epd *screen;
static uint8_t *frame;
static int minute_clock; // update every minute (else hourly, showing HH:00)

static void *psram_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static int clock_set(void) { return time(NULL) > 1700000000; }

static void local_now(struct tm *tm) {
  time_t now = time(NULL);
  if (CONFIG_FF_TIMEZONE[0]) {
    localtime_r(&now, tm);
  } else {
    time_t t = now + (st.have_offset ? st.utc_offset : 0);
    gmtime_r(&t, tm);
  }
}

static ff_time to_ff(const struct tm *tm) {
  return (ff_time){tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_wday};
}

// Where the frame is: from menuconfig, or estimated from the IP address.
static int find_place(char *buf, size_t size) {
  if (st.have_place) return 0;
  if (CONFIG_FF_LATITUDE[0] && CONFIG_FF_LONGITUDE[0]) {
    st.lat = strtod(CONFIG_FF_LATITUDE, NULL);
    st.lon = strtod(CONFIG_FF_LONGITUDE, NULL);
  } else if (net_get(IPINFO_URL, buf, size) != 200 || ipinfo_parse(buf, &st.lat, &st.lon)) {
    ESP_LOGW(TAG, "couldn't estimate the location from the IP address; set it in menuconfig");
    return -1;
  }
  st.have_place = 1;
  ESP_LOGI(TAG, "location %.4f, %.4f", st.lat, st.lon);
  return 0;
}

static void update_forecast(void) {
  static char buf[4096];
  char url[256];
  if (find_place(buf, sizeof buf)) return;
  forecast_url(url, sizeof url, st.lat, st.lon, FAHRENHEIT);
  forecast fc;
  if (net_get(url, buf, sizeof buf) != 200 || forecast_parse(buf, &fc)) {
    ESP_LOGW(TAG, "no forecast this time");
    return;
  }
  st.fc = fc;
  st.have_forecast = 1;
  st.utc_offset = fc.utc_offset;
  st.have_offset = 1;
  ESP_LOGI(TAG, "forecast for %s: code %d, %.1f/%.1f", fc.date, fc.code, fc.high, fc.low);
}

// Draw and show the frame for now, and note the hour's catch key.
static void show(int partial, char *key, size_t key_size) {
  struct tm tm;
  local_now(&tm);
  ff_time t = to_ff(&tm);

  ff_weather w;
  char today[16];
  snprintf(today, sizeof today, "%04d-%02d-%02d", t.year, t.month, t.day);
  int have_weather = st.have_forecast && !strcmp(st.fc.date, today);
  if (have_weather) w = (ff_weather){ff_sky_for_code(st.fc.code), st.fc.high, st.fc.low};

  ff_options opts = {
      .width = screen->driver->width,
      .height = screen->driver->height,
      .rotate = CONFIG_FF_ROTATE,
      .clock_12h = CLOCK_12H,
      .hourly = !minute_clock,
      .salt = CONFIG_FF_FRAME_ID,
      .weather = have_weather ? &w : NULL,
  };
  ff_bitmap bmp;
  ff_catch c;
  int64_t t0 = esp_timer_get_time();
  if (ff_render(&t, &opts, &bmp, &c)) {
    ESP_LOGE(TAG, "drawing the frame failed (out of memory?)");
    return;
  }
  int64_t t1 = esp_timer_get_time();
  epd_pack(bmp.pixels, bmp.width, bmp.height, frame);
  ff_bitmap_free(&bmp);
  epd_show(screen, frame, partial);
  ESP_LOGI(TAG, "%02d:%02d %s%s: drawn in %lld ms, shown in %lld ms", t.hour, t.minute, c.name,
           c.rare ? " (rare!)" : "", (long long)(t1 - t0) / 1000, (long long)(esp_timer_get_time() - t1) / 1000);
  snprintf(key, key_size, "%s", c.key);
}

static void wait_for_clock(void) {
  while (!clock_set()) {
    if (net_connect(30000) == 0) net_sync_time(30000);
    if (!clock_set()) {
      ESP_LOGW(TAG, "waiting for the time");
      vTaskDelay(pdMS_TO_TICKS(30000));
    }
  }
}

static void every_minute(void) {
  char key[32] = "";
  wait_for_clock();
  // The forecast also gives the UTC offset, so get it before the first frame.
  if (net_connect(20000) == 0) update_forecast();
  int fresh = 1;
  for (;;) {
    struct tm tm;
    local_now(&tm);
    ff_time t = to_ff(&tm);
    ff_catch c;
    ff_catch_for(&t, CONFIG_FF_FRAME_ID, &c);
    int new_hour = strcmp(c.key, key) != 0;
    if (new_hour && !fresh && net_connect(20000) == 0) update_forecast();
    fresh = 0;
    if (new_hour || minute_clock) show(!new_hour, key, sizeof key);

    // Sleep until just after the next minute starts.
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ms = 60000 - (tv.tv_sec % 60) * 1000 - tv.tv_usec / 1000 + 200;
    vTaskDelay(pdMS_TO_TICKS(ms));
  }
}

static void hourly(void) {
  if (net_connect(20000) == 0) {
    net_sync_time(20000);
    update_forecast();
  }
  if (clock_set()) {
    char key[32];
    show(0, key, sizeof key);
    epd_sleep(screen);
  } else {
    ESP_LOGW(TAG, "no time yet; trying again in 5 minutes");
  }
  net_stop();
  int64_t wait_s = 5 * 60;
  if (clock_set()) {
    struct tm tm;
    local_now(&tm);
    wait_s = 3600 - (tm.tm_min * 60 + tm.tm_sec) + 2; // just past the next local hour
  }
  ESP_LOGI(TAG, "sleeping for %lld s", (long long)wait_s);
  esp_deep_sleep((uint64_t)wait_s * 1000000);
}

// Draw a known fish and compare it, bit for bit, with what the JavaScript
// draws (test/esp32.test.js keeps EXPECTED in step). Also shows how long a
// fish takes on this board.
static void self_check(void) {
  static const char *NAME = "Biggus fishus";
  static const uint64_t EXPECTED = 0x63b536bb22e71ac0ull;
  int64_t t0 = esp_timer_get_time();
  fd_drawing d;
  if (fishdraw(NAME, &d)) {
    ESP_LOGE(TAG, "self-check: couldn't draw a fish (out of memory?)");
    return;
  }
  int64_t ms = (esp_timer_get_time() - t0) / 1000;
  uint64_t h = 0xcbf29ce484222325ull; // FNV-1a over each line's length and points
  for (int i = 0; i < d.n; i++) {
    int32_t n = d.lines[i].n;
    const uint8_t *b = (const uint8_t *)&n;
    for (int k = 0; k < 4; k++) h = (h ^ b[k]) * 0x100000001b3ull;
    b = (const uint8_t *)d.lines[i].points;
    for (size_t k = 0; k < (size_t)n * sizeof(fd_point); k++) h = (h ^ b[k]) * 0x100000001b3ull;
  }
  if (h == EXPECTED)
    ESP_LOGI(TAG, "self-check: \"%s\" matches the JavaScript exactly (%lld ms, %u KB working memory)", NAME,
             (long long)ms, (unsigned)(d.peak_bytes / 1024));
  else
    ESP_LOGW(TAG, "self-check: \"%s\" differs from the JavaScript (hash %016llx); check -ffp-contract=off", NAME,
             (unsigned long long)h);
  fishdraw_free(&d);
}

static void clock_task(void *arg) {
  (void)arg;
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) self_check();
  if (HOURLY) hourly();
  every_minute();
}

void app_main(void) {
  esp_err_t err = nvs_flash_init(); // Wi-Fi keeps calibration data here
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "PSRAM: %u KB", (unsigned)(psram / 1024));
  if (psram < 6 * 1024 * 1024)
    ESP_LOGW(TAG, "drawing a fish can take up to 4.2 MB; this board may run out (use an 8 MB PSRAM module)");
  fishdraw_set_allocator(psram_alloc, heap_caps_free);

  if (CONFIG_FF_TIMEZONE[0]) {
    setenv("TZ", CONFIG_FF_TIMEZONE, 1);
    tzset();
  }
  screen = panel_init();
  minute_clock = !HOURLY && screen->driver->partial;
  frame = heap_caps_malloc((size_t)(screen->driver->width + 7) / 8 * screen->driver->height, MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "panel: %s%s", screen->driver->name, minute_clock ? "" : ", updating hourly");
  // fishdraw recurses and works in doubles: give it a roomy stack, on the
  // second core so Wi-Fi keeps running while a fish is drawn.
  xTaskCreatePinnedToCore(clock_task, "clock", 64 * 1024, NULL, 5, NULL, 1);
}
