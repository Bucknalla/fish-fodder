# The C port of fish-fodder's renderer. `make` builds the command-line tools
# into c/build/; `npm test` uses them to check the C output against the JS.
#
# -ffp-contract=off is required: fused multiply-adds would change results and
# the same name would no longer draw the same fish. Needs IEEE 754 doubles
# without excess precision (x86-64, ARM, RISC-V and ESP32 are all fine; not
# 32-bit x86 builds that use the x87 FPU).

CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CFLAGS += -ffp-contract=off
BUILD := build

FISHDRAW := fishdraw.c fdlibm.c

all: $(BUILD)/fishdraw_cli $(BUILD)/frame_cli

$(BUILD)/fishdraw_cli: fishdraw_cli.c $(FISHDRAW) fishdraw.h fdlibm.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ fishdraw_cli.c $(FISHDRAW) -lm

$(BUILD)/frame_cli: frame_cli.c fishfodder.c $(FISHDRAW) fishfodder.h fishfodder_data.h fishdraw.h fdlibm.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ frame_cli.c fishfodder.c $(FISHDRAW) -lm

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
