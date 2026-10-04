// The e-paper panel chosen in menuconfig, on the shared drivers in c/epd.
#ifndef PANEL_H
#define PANEL_H

#include "epd.h"

// Set up the pins and SPI bus. Returns the panel, ready for epd_show().
epd *panel_init(void);

#endif
