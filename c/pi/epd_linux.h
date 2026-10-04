// The e-paper drivers on a Raspberry Pi (any Linux with spidev and the GPIO
// character device): pins, SPI and the Inky ID EEPROM, without libraries.
#ifndef EPD_LINUX_H
#define EPD_LINUX_H

#include "../epd/epd.h"

typedef struct {
  int rst, dc, cs, pwr, busy; // BCM GPIO numbers; -1 if not used
} epd_pins;

// The pins each vendor's HAT uses.
extern const epd_pins EPD_PINS_WAVESHARE; // RST 17, DC 25, BUSY 24, PWR 18; CS by SPI CE0
extern const epd_pins EPD_PINS_INKY;      // RST 27, DC 22, BUSY 17, CS 8 as a GPIO

typedef struct {
  epd panel;
  epd_io io;
  int spi_fd, out_fd, busy_fd, gpio_cs;
  int line_index[4]; // EPD_RST..EPD_PWR -> index in the output request, or -1
  unsigned bufsiz;
  char error[200];
} epd_linux;

// Open the panel. Returns 0, or -1 with a readable reason in dev->error.
int epd_linux_open(epd_linux *dev, const epd_driver *driver, const epd_pins *pins);
void epd_linux_close(epd_linux *dev);

// Pimoroni's ID EEPROM (inky/eeprom.py).
typedef struct {
  int width, height, colour, pcb_variant, display_variant;
  char written[23];
} inky_eeprom;

// Read it from I2C bus `bus` (1 on a Pi). Returns 0, or -1 if there's none.
int inky_read_eeprom(int bus, inky_eeprom *out);

// The panel on the header: an Inky board by its EEPROM, otherwise a
// Waveshare 7.5" V2 (which has no EEPROM). Sets *pins to match. Returns NULL,
// with a reason in `why`, for an Inky board without a driver here.
const epd_driver *epd_detect(const epd_pins **pins, char *why, size_t why_size);

#endif
