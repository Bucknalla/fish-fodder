// E-paper panel drivers, in portable C. Each driver sends the same commands
// and data as its vendor's own driver (test/epd-parity.test.js compares the
// byte streams); the platform supplies SPI, pins and delays through epd_io.
//
// Frames are 1 bit per pixel, MSB first, rows padded to whole bytes, with a
// set bit for paper (white) and a clear bit for ink.
#ifndef EPD_H
#define EPD_H

#include <stddef.h>
#include <stdint.h>

enum { EPD_RST, EPD_DC, EPD_CS, EPD_PWR };

// What the platform provides.
typedef struct {
  void *ctx;
  void (*spi)(void *ctx, const uint8_t *data, size_t n); // write bytes
  void (*pin)(void *ctx, int pin, int level);            // set RST, DC, CS or PWR
  int (*busy)(void *ctx);                                // read BUSY (1 = high)
  void (*delay_ms)(void *ctx, int ms);
} epd_io;

typedef struct epd_driver epd_driver;

enum { EPD_ASLEEP, EPD_AWAKE, EPD_PARTIAL_MODE };

// A panel and what it's doing.
typedef struct {
  const epd_driver *driver;
  const epd_io *io;
  int mode; // EPD_ASLEEP (or unknown) to start with
} epd;

struct epd_driver {
  const char *name;
  int width, height;
  int partial;  // can update without a full refresh
  int spi_hz;   // the vendor driver's SPI clock
  int gpio_cs;  // chip select is driven as a GPIO (else by the SPI hardware)
  int busy_pull; // BUSY bias: 1 pull-up, -1 pull-down, 0 none
  int reset_idle; // the level RST rests at before the first reset
  void (*show)(epd *e, const uint8_t *frame, int partial);
  void (*sleep)(epd *e);
};

extern const epd_driver epd_waveshare_7in5_v2; // Waveshare 7.5" e-Paper V2, 800x480
extern const epd_driver epd_inky_ac073tc1a;    // Inky Impression 7.3" (7-colour), 800x480
extern const epd_driver epd_inky_e673;         // Inky Impression 7.3" (Spectra 6), 800x480

// Show a frame. A partial refresh, where the panel supports it, changes only
// what differs from the last frame without flashing; otherwise it's a full
// refresh. The panel may stay powered up afterwards, ready for the next
// partial refresh; call epd_sleep when there won't be one for a while.
void epd_show(epd *e, const uint8_t *frame, int partial);
void epd_sleep(epd *e);

// The driver for an Inky board's ID EEPROM display variant, or NULL.
const epd_driver *epd_inky_driver(int display_variant);
const char *epd_inky_variant_name(int display_variant);

// Pack a 1-byte-per-pixel bitmap (1 = ink) into a frame.
void epd_pack(const uint8_t *pixels, int width, int height, uint8_t *frame);

#endif
