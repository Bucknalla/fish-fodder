// ESP-IDF glue for the e-paper drivers in c/epd: SPI, pins and delays. Chip
// select is driven as a GPIO, which suits every driver.

#include "panel.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if defined(CONFIG_FF_PANEL_INKY_E673)
#define DRIVER epd_inky_e673
#elif defined(CONFIG_FF_PANEL_INKY_AC073TC1A)
#define DRIVER epd_inky_ac073tc1a
#else
#define DRIVER epd_waveshare_7in5_v2
#endif

#define CHUNK 4096

static spi_device_handle_t spi;
static DMA_ATTR uint8_t chunk[CHUNK];
static const int PINS[] = {[EPD_RST] = CONFIG_FF_PIN_RST,
                           [EPD_DC] = CONFIG_FF_PIN_DC,
                           [EPD_CS] = CONFIG_FF_PIN_CS,
                           [EPD_PWR] = CONFIG_FF_PIN_PWR};

static void io_spi(void *ctx, const uint8_t *p, size_t n) {
  (void)ctx;
  while (n) {
    size_t k = n < CHUNK ? n : CHUNK;
    memcpy(chunk, p, k);
    spi_transaction_t t = {.length = k * 8, .tx_buffer = chunk};
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
    p += k;
    n -= k;
  }
}

static void io_pin(void *ctx, int pin, int level) {
  (void)ctx;
  if (PINS[pin] >= 0) gpio_set_level(PINS[pin], level);
}

static int io_busy(void *ctx) {
  (void)ctx;
  return gpio_get_level(CONFIG_FF_PIN_BUSY);
}

static void io_delay(void *ctx, int ms) {
  (void)ctx;
  if (ms < 20) esp_rom_delay_us(ms * 1000); // shorter than a couple of ticks
  else vTaskDelay(pdMS_TO_TICKS(ms));
}

static const epd_io io = {NULL, io_spi, io_pin, io_busy, io_delay};
static epd panel = {&DRIVER, &io, EPD_ASLEEP};

epd *panel_init(void) {
  uint64_t outputs = 0;
  for (int i = 0; i < 4; i++)
    if (PINS[i] >= 0) outputs |= 1ULL << PINS[i];
  gpio_config_t out = {.pin_bit_mask = outputs, .mode = GPIO_MODE_OUTPUT};
  ESP_ERROR_CHECK(gpio_config(&out));
  // The levels the vendors' drivers start at.
  gpio_set_level(CONFIG_FF_PIN_CS, 1);
  gpio_set_level(CONFIG_FF_PIN_DC, 0);
  gpio_set_level(CONFIG_FF_PIN_RST, DRIVER.reset_idle);
#if CONFIG_FF_PIN_PWR >= 0
  gpio_set_level(CONFIG_FF_PIN_PWR, 0);
#endif
  gpio_config_t in = {
      .pin_bit_mask = 1ULL << CONFIG_FF_PIN_BUSY,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = DRIVER.busy_pull > 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
      .pull_down_en = DRIVER.busy_pull < 0 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&in));

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
      .clock_speed_hz = DRIVER.spi_hz,
      .mode = 0,
      .spics_io_num = -1,
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));
  return &panel;
}
