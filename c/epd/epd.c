// E-paper drivers: command sequences from the vendors' own drivers.
//
// Waveshare 7.5" V2: waveshare_epd/epd7in5_V2.py (Waveshare, MIT licence),
//   the newest of Waveshare's drivers for it (September 2024).
// Inky Impression 7.3": inky/inky_ac073tc1a.py and inky/inky_e673.py
//   (Pimoroni, MIT licence).
//
// Calls mirror the vendors' call for call (which byte goes in which SPI
// write, when CS and DC change, every delay), so the byte streams can be
// compared exactly; see test/epd-parity.test.js.

#include "epd.h"

#include <string.h>

#define IO (e->io)
#define PIN(p, v) IO->pin(IO->ctx, (p), (v))
#define SPI(d, n) IO->spi(IO->ctx, (d), (n))
#define DELAY(ms) IO->delay_ms(IO->ctx, (ms))
#define BUSY() IO->busy(IO->ctx)
#define BYTES(...) (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__})

void epd_show(epd *e, const uint8_t *frame, int partial) { e->driver->show(e, frame, partial); }
void epd_sleep(epd *e) { e->driver->sleep(e); }

void epd_pack(const uint8_t *pixels, int width, int height, uint8_t *frame) {
  int stride = (width + 7) / 8;
  memset(frame, 0xFF, (size_t)stride * height);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (pixels[y * width + x]) frame[y * stride + (x >> 3)] &= ~(0x80 >> (x & 7));
}

// ---------------------------------------------------------------------------
// Waveshare 7.5" e-Paper V2 (UC8179-style controller)

#define WS_W 800
#define WS_H 480
#define WS_BYTES (WS_W / 8 * WS_H)

static void ws_command(epd *e, uint8_t c) {
  PIN(EPD_DC, 0);
  PIN(EPD_CS, 0);
  SPI(&c, 1);
  PIN(EPD_CS, 1);
}

static void ws_data(epd *e, uint8_t d) {
  PIN(EPD_DC, 1);
  PIN(EPD_CS, 0);
  SPI(&d, 1);
  PIN(EPD_CS, 1);
}

static void ws_data_bytes(epd *e, const uint8_t *p, size_t n) {
  PIN(EPD_DC, 1);
  PIN(EPD_CS, 0);
  SPI(p, n);
  PIN(EPD_CS, 1);
}

// Waveshare's driver repeats "get status" (0x71) while BUSY is low; this
// adds a millisecond between polls and gives up after a minute.
static void ws_busy(epd *e) {
  ws_command(e, 0x71);
  for (int waited = 0; !BUSY() && waited < 60000; waited++) {
    DELAY(1);
    ws_command(e, 0x71);
  }
  DELAY(20);
}

static void ws_reset(epd *e) {
  PIN(EPD_RST, 1);
  DELAY(20);
  PIN(EPD_RST, 0);
  DELAY(2);
  PIN(EPD_RST, 1);
  DELAY(20);
}

static void ws_init(epd *e) {
  PIN(EPD_PWR, 1); // module_init
  ws_reset(e);
  ws_command(e, 0x06); // booster soft start
  ws_data(e, 0x17);
  ws_data(e, 0x17);
  ws_data(e, 0x28);
  ws_data(e, 0x17);
  ws_command(e, 0x01); // power setting
  ws_data(e, 0x07);
  ws_data(e, 0x07);
  ws_data(e, 0x28);
  ws_data(e, 0x17);
  ws_command(e, 0x04); // power on
  DELAY(100);
  ws_busy(e);
  ws_command(e, 0x00); // panel setting
  ws_data(e, 0x1F);
  ws_command(e, 0x61); // resolution 800 x 480
  ws_data(e, 0x03);
  ws_data(e, 0x20);
  ws_data(e, 0x01);
  ws_data(e, 0xE0);
  ws_command(e, 0x15);
  ws_data(e, 0x00);
  ws_command(e, 0x50); // VCOM and data interval
  ws_data(e, 0x10);
  ws_data(e, 0x07);
  ws_command(e, 0x60); // TCON
  ws_data(e, 0x22);
}

static void ws_init_partial(epd *e) {
  PIN(EPD_PWR, 1);
  ws_reset(e);
  ws_command(e, 0x00);
  ws_data(e, 0x1F);
  ws_command(e, 0x04);
  DELAY(100);
  ws_busy(e);
  ws_command(e, 0xE0);
  ws_data(e, 0x02);
  ws_command(e, 0xE5);
  ws_data(e, 0x6E);
}

static void ws_refresh(epd *e) {
  ws_command(e, 0x12);
  DELAY(100);
  ws_busy(e);
}

static void ws_show(epd *e, const uint8_t *frame, int partial) {
  if (partial) {
    if (e->mode != EPD_PARTIAL_MODE) {
      ws_init_partial(e);
      e->mode = EPD_PARTIAL_MODE;
    }
    ws_command(e, 0x50);
    ws_data(e, 0xA9);
    ws_data(e, 0x07);
    ws_command(e, 0x91); // partial mode
    ws_command(e, 0x90); // over the whole panel: x 0..799, y 0..479
    ws_data(e, 0x00);
    ws_data(e, 0x00);
    ws_data(e, 0x03);
    ws_data(e, 0x1F);
    ws_data(e, 0x00);
    ws_data(e, 0x00);
    ws_data(e, 0x01);
    ws_data(e, 0xDF);
    ws_data(e, 0x01);
    ws_command(e, 0x13);
    ws_data_bytes(e, frame, WS_BYTES); // bit set = white
    ws_refresh(e);
    return;
  }
  static uint8_t ink[WS_BYTES];
  for (size_t i = 0; i < WS_BYTES; i++) ink[i] = ~frame[i];
  ws_init(e);
  ws_command(e, 0x10);
  ws_data_bytes(e, frame, WS_BYTES); // bit set = white
  ws_command(e, 0x13);
  ws_data_bytes(e, ink, WS_BYTES); // bit set = black
  ws_refresh(e);
  e->mode = EPD_AWAKE;
}

static void ws_sleep(epd *e) {
  if (e->mode == EPD_ASLEEP) return;
  ws_command(e, 0x50);
  ws_data(e, 0xF7);
  ws_command(e, 0x02); // power off
  ws_busy(e);
  ws_command(e, 0x07); // deep sleep
  ws_data(e, 0xA5);
  DELAY(2000);
  PIN(EPD_RST, 0); // module_exit
  PIN(EPD_DC, 0);
  PIN(EPD_PWR, 0);
  e->mode = EPD_ASLEEP;
}

const epd_driver epd_waveshare_7in5_v2 = {
    .name = "Waveshare 7.5\" e-Paper V2",
    .width = WS_W,
    .height = WS_H,
    .partial = 1,
    .spi_hz = 4000000,
    .gpio_cs = 0,
    .busy_pull = -1,
    .reset_idle = 0,
    .show = ws_show,
    .sleep = ws_sleep,
};

// ---------------------------------------------------------------------------
// Inky Impression 7.3": shared pieces. Both panels take 4 bits per pixel,
// with black 0 and white 1, and have no partial refresh or sleep command.

#define INKY_W 800
#define INKY_H 480
#define INKY_BYTES (INKY_W * INKY_H / 2)

static void inky_pixels(const uint8_t *frame, uint8_t *buf) {
  for (int y = 0; y < INKY_H; y++) {
    const uint8_t *row = frame + y * (INKY_W / 8);
    for (int x = 0; x < INKY_W; x += 2) {
      int a = (row[x >> 3] >> (7 - (x & 7))) & 1;
      int b = (row[(x + 1) >> 3] >> (7 - ((x + 1) & 7))) & 1;
      buf[(y * INKY_W + x) / 2] = (uint8_t)(a << 4 | b);
    }
  }
}

// Wait for BUSY to go high (ready), as Pimoroni's driver does: if it's
// already high, the board may not be driving it, so wait the whole timeout.
static void inky_busy(epd *e, int timeout_ms, int poll_ms) {
  if (BUSY()) {
    DELAY(timeout_ms);
    return;
  }
  for (int waited = 0; !BUSY() && waited <= timeout_ms; waited += poll_ms) DELAY(poll_ms);
}

static void inky_nothing(epd *e) { (void)e; }

// ---------------------------------------------------------------------------
// Inky Impression 7.3" (2023, 7-colour AC073TC1A)

static void ac_write(epd *e, int dc, const uint8_t *p, size_t n) {
  PIN(EPD_CS, 0);
  PIN(EPD_DC, dc);
  SPI(p, n);
  PIN(EPD_CS, 1);
}

static void ac_command(epd *e, uint8_t c, const uint8_t *data, size_t n) {
  ac_write(e, 0, &c, 1);
  if (data) ac_write(e, 1, data, n);
}

static void ac_setup(epd *e) {
  PIN(EPD_RST, 0);
  DELAY(100);
  PIN(EPD_RST, 1);
  DELAY(100);
  PIN(EPD_RST, 0);
  DELAY(100);
  PIN(EPD_RST, 1);
  inky_busy(e, 1000, 10);
  ac_command(e, 0xAA, BYTES(0x49, 0x55, 0x20, 0x08, 0x09, 0x18));
  ac_command(e, 0x01, BYTES(0x3F, 0x00, 0x32, 0x2A, 0x0E, 0x2A));
  ac_command(e, 0x00, BYTES(0x5F, 0x69));
  ac_command(e, 0x03, BYTES(0x00, 0x54, 0x00, 0x44));
  ac_command(e, 0x05, BYTES(0x40, 0x1F, 0x1F, 0x2C));
  ac_command(e, 0x06, BYTES(0x6F, 0x1F, 0x16, 0x25));
  ac_command(e, 0x08, BYTES(0x6F, 0x1F, 0x1F, 0x22));
  ac_command(e, 0x13, BYTES(0x00, 0x04));
  ac_command(e, 0x30, BYTES(0x02));
  ac_command(e, 0x41, BYTES(0x00));
  ac_command(e, 0x50, BYTES((1 << 5) | 0x17)); // white border
  ac_command(e, 0x60, BYTES(0x02, 0x00));
  ac_command(e, 0x61, BYTES(0x03, 0x20, 0x01, 0xE0));
  ac_command(e, 0x82, BYTES(0x1E));
  ac_command(e, 0x84, BYTES(0x00));
  ac_command(e, 0x86, BYTES(0x00));
  ac_command(e, 0xE3, BYTES(0x2F));
  ac_command(e, 0xE0, BYTES(0x00));
  ac_command(e, 0xE6, BYTES(0x00));
}

static void ac_show(epd *e, const uint8_t *frame, int partial) {
  (void)partial;
  static uint8_t buf[INKY_BYTES];
  inky_pixels(frame, buf);
  ac_setup(e);
  ac_command(e, 0x10, buf, INKY_BYTES);
  ac_command(e, 0x04, NULL, 0); // power on
  inky_busy(e, 400, 10);
  ac_command(e, 0x12, BYTES(0x00)); // refresh
  inky_busy(e, 45000, 10);
  ac_command(e, 0x02, BYTES(0x00)); // power off
  inky_busy(e, 400, 10);
  e->mode = EPD_ASLEEP;
}

const epd_driver epd_inky_ac073tc1a = {
    .name = "Inky Impression 7.3\" (7-colour)",
    .width = INKY_W,
    .height = INKY_H,
    .partial = 0,
    .spi_hz = 5000000,
    .gpio_cs = 1,
    .busy_pull = 0,
    .reset_idle = 1,
    .show = ac_show,
    .sleep = inky_nothing,
};

// ---------------------------------------------------------------------------
// Inky Impression 7.3" (2025, Spectra 6 E673)

static void e6_command(epd *e, uint8_t c, const uint8_t *data, size_t n) {
  PIN(EPD_CS, 0);
  PIN(EPD_DC, 0);
  DELAY(300);
  SPI(&c, 1);
  if (data) {
    PIN(EPD_DC, 1);
    SPI(data, n);
  }
  PIN(EPD_CS, 1);
  PIN(EPD_DC, 0);
}

static void e6_setup(epd *e) {
  PIN(EPD_RST, 0);
  DELAY(30);
  PIN(EPD_RST, 1);
  DELAY(30);
  inky_busy(e, 300, 100);
  e6_command(e, 0xAA, BYTES(0x49, 0x55, 0x20, 0x08, 0x09, 0x18));
  e6_command(e, 0x01, BYTES(0x3F));
  e6_command(e, 0x00, BYTES(0x5F, 0x69));
  e6_command(e, 0x05, BYTES(0x40, 0x1F, 0x1F, 0x2C));
  e6_command(e, 0x08, BYTES(0x6F, 0x1F, 0x1F, 0x22));
  e6_command(e, 0x06, BYTES(0x6F, 0x1F, 0x17, 0x17));
  e6_command(e, 0x03, BYTES(0x00, 0x54, 0x00, 0x44));
  e6_command(e, 0x60, BYTES(0x02, 0x00));
  e6_command(e, 0x30, BYTES(0x08));
  e6_command(e, 0x50, BYTES(0x3F));
  e6_command(e, 0x61, BYTES(0x03, 0x20, 0x01, 0xE0));
  e6_command(e, 0xE3, BYTES(0x2F));
  e6_command(e, 0x82, BYTES(0x01));
}

static void e6_show(epd *e, const uint8_t *frame, int partial) {
  (void)partial;
  static uint8_t buf[INKY_BYTES];
  inky_pixels(frame, buf);
  e6_setup(e);
  e6_command(e, 0x10, buf, INKY_BYTES);
  e6_command(e, 0x04, NULL, 0); // power on
  inky_busy(e, 300, 100);
  e6_command(e, 0x06, BYTES(0x6F, 0x1F, 0x17, 0x49)); // booster, second setting
  e6_command(e, 0x12, BYTES(0x00));                   // refresh
  inky_busy(e, 32000, 100);
  e6_command(e, 0x02, BYTES(0x00)); // power off
  inky_busy(e, 300, 100);
  e6_command(e, 0x00, BYTES(0x4F, 0x6E));
  inky_busy(e, 300, 100);
  e->mode = EPD_ASLEEP;
}

const epd_driver epd_inky_e673 = {
    .name = "Inky Impression 7.3\" (Spectra 6)",
    .width = INKY_W,
    .height = INKY_H,
    .partial = 0,
    .spi_hz = 1000000,
    .gpio_cs = 1,
    .busy_pull = 1,
    .reset_idle = 1,
    .show = e6_show,
    .sleep = inky_nothing,
};

// ---------------------------------------------------------------------------
// Inky ID EEPROM display variants (inky/eeprom.py)

static const char *const INKY_VARIANTS[] = {
    NULL, "Red pHAT (High-Temp)", "Yellow wHAT", "Black wHAT", "Black pHAT", "Yellow pHAT", "Red wHAT",
    "Red wHAT (High-Temp)", "Red wHAT", NULL, "Black pHAT (SSD1608)", "Red pHAT (SSD1608)",
    "Yellow pHAT (SSD1608)", NULL, "7-Colour (UC8159)", "7-Colour 640x400 (UC8159)", "7-Colour 640x400 (UC8159)",
    "Black wHAT (SSD1683)", "Red wHAT (SSD1683)", "Yellow wHAT (SSD1683)", "7-Colour 800x480 (AC073TC1A)",
    "Spectra 6 13.3 1600 x 1200 (EL133UF1)", "Spectra 6 7.3 800 x 480 (E673)", "Red/Yellow pHAT (JD79661)",
    "Red/Yellow wHAT (JD79668)", "Spectra 6 4.0 600 x 400 (E640)", "Spectra 6 7.3 800 x 480 (E673) AC",
    "Spectra 6 13.3 1600 x 1200 (EL133UF1) AC", "Red/Yellow wHAT (SSD2683)",
};

const char *epd_inky_variant_name(int v) {
  if (v < 0 || v >= (int)(sizeof INKY_VARIANTS / sizeof *INKY_VARIANTS)) return NULL;
  return INKY_VARIANTS[v];
}

const epd_driver *epd_inky_driver(int v) {
  if (v == 20) return &epd_inky_ac073tc1a;
  if (v == 22 || v == 26) return &epd_inky_e673;
  return NULL;
}
