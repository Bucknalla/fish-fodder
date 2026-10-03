// Waveshare 7.5" e-Paper V2 (800 x 480, black and white) over SPI.
#ifndef EPD_H
#define EPD_H

#include <stdint.h>

#define EPD_WIDTH 800
#define EPD_HEIGHT 480
#define EPD_BYTES (EPD_WIDTH / 8 * EPD_HEIGHT)

// Set up the SPI bus and pins (from menuconfig).
void epd_init(void);

// Show a frame: EPD_BYTES, 1 bit per pixel, MSB first, bit set = paper.
// A partial refresh changes only what differs from the last frame, without
// the full refresh's flashing. The panel sleeps afterwards either way.
void epd_show(const uint8_t *frame, int partial);

// Pack a fish-fodder bitmap (1 byte per pixel, 1 = ink) for epd_show.
void epd_pack(const uint8_t *pixels, uint8_t *frame);

#endif
