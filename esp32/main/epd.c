// Waveshare 7.5" e-Paper V2 driver. The command sequences follow Waveshare's
// own driver (EPD_7in5_V2.c, MIT licence, waveshareteam/e-Paper).

#include "epd.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define STRIDE (EPD_WIDTH / 8)
#define CHUNK 4096

static const char *TAG = "epd";
static spi_device_handle_t spi;
static DMA_ATTR uint8_t chunk[CHUNK];
static uint8_t *shown; // what's on the panel, for partial refreshes
static int have_shown;

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }

static void send(int dc, const uint8_t *p, size_t n, int invert) {
  gpio_set_level(CONFIG_FF_PIN_DC, dc);
  while (n) {
    size_t k = n < CHUNK ? n : CHUNK;
    for (size_t i = 0; i < k; i++) chunk[i] = invert ? ~p[i] : p[i];
    spi_transaction_t t = {.length = k * 8, .tx_buffer = chunk};
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
    p += k;
    n -= k;
  }
}

static void cmd(uint8_t c) { send(0, &c, 1, 0); }

#define DATA(...)                                \
  do {                                           \
    static const uint8_t d_[] = {__VA_ARGS__};   \
    send(1, d_, sizeof d_, 0);                   \
  } while (0)

// BUSY is low while the panel works.
static void wait_idle(void) {
  int64_t t0 = esp_timer_get_time();
  do {
    delay_ms(5);
    if (esp_timer_get_time() - t0 > 60 * 1000000LL) {
      ESP_LOGW(TAG, "panel still busy after 60 s; check the BUSY pin");
      return;
    }
  } while (!gpio_get_level(CONFIG_FF_PIN_BUSY));
  delay_ms(5);
}

static void reset(void) {
  gpio_set_level(CONFIG_FF_PIN_RST, 1);
  delay_ms(20);
  gpio_set_level(CONFIG_FF_PIN_RST, 0);
  delay_ms(2);
  gpio_set_level(CONFIG_FF_PIN_RST, 1);
  delay_ms(20);
}

static void init_full(void) {
  reset();
  cmd(0x01); // power setting
  DATA(0x07, 0x07, 0x3f, 0x3f);
  cmd(0x06); // booster soft start
  DATA(0x17, 0x17, 0x28, 0x17);
  cmd(0x04); // power on
  delay_ms(100);
  wait_idle();
  cmd(0x00); // panel setting: black and white, LUT from OTP
  DATA(0x1F);
  cmd(0x61); // resolution: 800 x 480
  DATA(0x03, 0x20, 0x01, 0xE0);
  cmd(0x15);
  DATA(0x00);
  cmd(0x50); // VCOM and data interval
  DATA(0x10, 0x07);
  cmd(0x60); // TCON
  DATA(0x22);
}

static void init_partial(void) {
  reset();
  cmd(0x00);
  DATA(0x1F);
  cmd(0x04);
  delay_ms(100);
  wait_idle();
  cmd(0xE0);
  DATA(0x02);
  cmd(0xE5);
  DATA(0x6E);
}

static void refresh(void) {
  cmd(0x12);
  delay_ms(100);
  wait_idle();
}

static void panel_sleep(void) {
  cmd(0x50);
  DATA(0xF7);
  cmd(0x02); // power off
  wait_idle();
  cmd(0x07); // deep sleep
  DATA(0xA5);
}

void epd_init(void) {
  gpio_config_t out = {
      .pin_bit_mask = (1ULL << CONFIG_FF_PIN_DC) | (1ULL << CONFIG_FF_PIN_RST),
      .mode = GPIO_MODE_OUTPUT,
  };
#if CONFIG_FF_PIN_PWR >= 0
  out.pin_bit_mask |= 1ULL << CONFIG_FF_PIN_PWR;
#endif
  ESP_ERROR_CHECK(gpio_config(&out));
  gpio_config_t in = {.pin_bit_mask = 1ULL << CONFIG_FF_PIN_BUSY, .mode = GPIO_MODE_INPUT};
  ESP_ERROR_CHECK(gpio_config(&in));
#if CONFIG_FF_PIN_PWR >= 0
  gpio_set_level(CONFIG_FF_PIN_PWR, 1); // newer Waveshare boards switch the panel's power
#endif

  spi_bus_config_t bus = {
      .mosi_io_num = CONFIG_FF_PIN_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = CONFIG_FF_PIN_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = CHUNK,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  spi_device_interface_config_t dev = {
      .clock_speed_hz = 4 * 1000 * 1000,
      .mode = 0,
      .spics_io_num = CONFIG_FF_PIN_CS,
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));
  shown = malloc(EPD_BYTES);
}

void epd_show(const uint8_t *frame, int partial) {
  if (partial && have_shown) {
    init_partial();
    cmd(0x50);
    DATA(0xA9, 0x07);
    cmd(0x91); // partial mode, over the whole panel
    cmd(0x90);
    DATA(0x00, 0x00, 0x03, 0x1F, 0x00, 0x00, 0x01, 0xDF, 0x01);
    // The panel forgot the old frame when it slept; it needs it to work out
    // which pixels change.
    cmd(0x10);
    send(1, shown, EPD_BYTES, 0);
    cmd(0x13);
    send(1, frame, EPD_BYTES, 0);
  } else {
    init_full();
    cmd(0x10);
    send(1, frame, EPD_BYTES, 0);
    cmd(0x13);
    send(1, frame, EPD_BYTES, 1);
  }
  refresh();
  panel_sleep();
  if (shown) {
    memcpy(shown, frame, EPD_BYTES);
    have_shown = 1;
  }
}

void epd_pack(const uint8_t *pixels, uint8_t *frame) {
  memset(frame, 0xFF, EPD_BYTES);
  for (int y = 0; y < EPD_HEIGHT; y++) {
    const uint8_t *row = pixels + y * EPD_WIDTH;
    for (int x = 0; x < EPD_WIDTH; x++)
      if (row[x]) frame[y * STRIDE + (x >> 3)] &= ~(0x80 >> (x & 7));
  }
}
