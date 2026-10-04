#define _GNU_SOURCE
#include "epd_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

const epd_pins EPD_PINS_WAVESHARE = {.rst = 17, .dc = 25, .cs = -1, .pwr = 18, .busy = 24};
const epd_pins EPD_PINS_INKY = {.rst = 27, .dc = 22, .cs = 8, .pwr = -1, .busy = 17};

static void lx_spi(void *ctx, const uint8_t *data, size_t n) {
  epd_linux *d = ctx;
  while (n) {
    size_t k = n < d->bufsiz ? n : d->bufsiz;
    struct spi_ioc_transfer t = {.tx_buf = (unsigned long)data, .len = (unsigned)k, .bits_per_word = 8};
    if (ioctl(d->spi_fd, SPI_IOC_MESSAGE(1), &t) < 0) perror("epd: spi write");
    data += k;
    n -= k;
  }
}

static void lx_pin(void *ctx, int pin, int level) {
  epd_linux *d = ctx;
  int i = d->line_index[pin];
  if (i < 0) return; // not wired (CS on the SPI hardware, or no PWR)
  struct gpio_v2_line_values v = {.bits = (uint64_t)(level ? 1 : 0) << i, .mask = 1ull << i};
  if (ioctl(d->out_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0) perror("epd: gpio set");
}

static int lx_busy(void *ctx) {
  epd_linux *d = ctx;
  struct gpio_v2_line_values v = {.mask = 1};
  if (ioctl(d->busy_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) {
    perror("epd: gpio get");
    return 1;
  }
  return (int)(v.bits & 1);
}

static void lx_delay(void *ctx, int ms) {
  (void)ctx;
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
  while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
  }
}

// The SoC's GPIO controller: pinctrl-bcm2835 on a Pi Zero to 4, rp1 on a 5.
static int open_gpiochip(void) {
  for (int i = 0; i < 16; i++) {
    char path[32];
    snprintf(path, sizeof path, "/dev/gpiochip%d", i);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) continue;
    struct gpiochip_info info;
    if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 && !strncmp(info.label, "pinctrl-", 8)) return fd;
    close(fd);
  }
  return open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
}

int epd_linux_open(epd_linux *dev, const epd_driver *driver, const epd_pins *pins) {
  memset(dev, 0, sizeof *dev);
  dev->spi_fd = dev->out_fd = dev->busy_fd = -1;
  dev->io = (epd_io){dev, lx_spi, lx_pin, lx_busy, lx_delay};
  dev->panel = (epd){driver, &dev->io, 0};
  dev->gpio_cs = driver->gpio_cs && pins->cs >= 0;

  // Output pins, at the levels the vendors' drivers start them at.
  int chip = open_gpiochip();
  if (chip < 0) {
    snprintf(dev->error, sizeof dev->error, "can't open the GPIO controller: %s", strerror(errno));
    return -1;
  }
  struct gpio_v2_line_request out = {.num_lines = 0};
  snprintf(out.consumer, sizeof out.consumer, "fish-fodder");
  const int want[4] = {pins->rst, pins->dc, dev->gpio_cs ? pins->cs : -1, pins->pwr};
  const int idle[4] = {driver->reset_idle, 0, 1, 0};
  uint64_t initial = 0;
  for (int p = 0; p < 4; p++) {
    dev->line_index[p] = -1;
    if (want[p] < 0) continue;
    dev->line_index[p] = (int)out.num_lines;
    if (idle[p]) initial |= 1ull << out.num_lines;
    out.offsets[out.num_lines++] = (unsigned)want[p];
  }
  out.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
  out.config.num_attrs = 1;
  out.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
  out.config.attrs[0].attr.values = initial;
  out.config.attrs[0].mask = (1ull << out.num_lines) - 1;
  if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &out) < 0) {
    int err = errno;
    close(chip);
    if (err == EBUSY && dev->gpio_cs)
      snprintf(dev->error, sizeof dev->error,
               "GPIO %d is in use, probably as SPI chip select: Inky boards need "
               "dtoverlay=spi0-0cs in /boot/firmware/config.txt",
               pins->cs);
    else
      snprintf(dev->error, sizeof dev->error, "can't claim the panel's pins: %s", strerror(err));
    return -1;
  }
  dev->out_fd = out.fd;

  struct gpio_v2_line_request in = {.num_lines = 1};
  snprintf(in.consumer, sizeof in.consumer, "fish-fodder");
  in.offsets[0] = (unsigned)pins->busy;
  in.config.flags = GPIO_V2_LINE_FLAG_INPUT | (driver->busy_pull > 0   ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP
                                               : driver->busy_pull < 0 ? GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN
                                                                       : 0);
  int ok = ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &in) == 0;
  int err = errno;
  close(chip);
  if (!ok) {
    snprintf(dev->error, sizeof dev->error, "can't claim the BUSY pin (GPIO %d): %s", pins->busy, strerror(err));
    epd_linux_close(dev);
    return -1;
  }
  dev->busy_fd = in.fd;

  dev->spi_fd = open("/dev/spidev0.0", O_RDWR | O_CLOEXEC);
  if (dev->spi_fd < 0) {
    snprintf(dev->error, sizeof dev->error, "can't open /dev/spidev0.0 (%s): enable SPI with raspi-config",
             strerror(errno));
    epd_linux_close(dev);
    return -1;
  }
  uint32_t mode = SPI_MODE_0 | (dev->gpio_cs ? SPI_NO_CS : 0);
  if (ioctl(dev->spi_fd, SPI_IOC_WR_MODE32, &mode) < 0) {
    mode = SPI_MODE_0; // as Pimoroni's driver does when no_cs isn't supported
    ioctl(dev->spi_fd, SPI_IOC_WR_MODE32, &mode);
  }
  uint32_t hz = (uint32_t)driver->spi_hz;
  ioctl(dev->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz);

  // spidev's largest transfer.
  dev->bufsiz = 4096;
  FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r");
  if (f) {
    unsigned b;
    if (fscanf(f, "%u", &b) == 1 && b > 0) dev->bufsiz = b;
    fclose(f);
  }
  return 0;
}

void epd_linux_close(epd_linux *dev) {
  if (dev->spi_fd >= 0) close(dev->spi_fd);
  if (dev->out_fd >= 0) close(dev->out_fd);
  if (dev->busy_fd >= 0) close(dev->busy_fd);
  dev->spi_fd = dev->out_fd = dev->busy_fd = -1;
}

int inky_read_eeprom(int bus, inky_eeprom *out) {
  char path[32];
  snprintf(path, sizeof path, "/dev/i2c-%d", bus);
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) return -1;
  // As inky/eeprom.py does it: set the address to 0, then read 29 bytes.
  uint8_t zero[2] = {0, 0}, data[29];
  struct i2c_msg set = {.addr = 0x50, .flags = 0, .len = 2, .buf = zero};
  struct i2c_msg read[2] = {
      {.addr = 0x50, .flags = 0, .len = 1, .buf = zero},
      {.addr = 0x50, .flags = I2C_M_RD, .len = sizeof data, .buf = data},
  };
  struct i2c_rdwr_ioctl_data a = {&set, 1}, b = {read, 2};
  int ok = ioctl(fd, I2C_RDWR, &a) >= 0 && ioctl(fd, I2C_RDWR, &b) >= 0;
  close(fd);
  if (!ok) return -1;
  // struct "<HHBBB22p"
  out->width = data[0] | data[1] << 8;
  out->height = data[2] | data[3] << 8;
  out->colour = data[4];
  out->pcb_variant = data[5];
  out->display_variant = data[6];
  int n = data[7] < 21 ? data[7] : 21;
  memcpy(out->written, data + 8, (size_t)n);
  out->written[n] = 0;
  return 0;
}

const epd_driver *epd_detect(const epd_pins **pins, char *why, size_t why_size) {
  inky_eeprom e;
  if (inky_read_eeprom(1, &e)) {
    *pins = &EPD_PINS_WAVESHARE;
    return &epd_waveshare_7in5_v2;
  }
  *pins = &EPD_PINS_INKY;
  const epd_driver *d = epd_inky_driver(e.display_variant);
  if (!d) {
    const char *name = epd_inky_variant_name(e.display_variant);
    snprintf(why, why_size, "found an Inky board (%s, %dx%d) that has no driver here yet",
             name ? name : "unknown variant", e.width, e.height);
  }
  return d;
}
