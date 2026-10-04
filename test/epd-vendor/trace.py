#!/usr/bin/env python3
"""Run a vendor e-paper driver against fake hardware and print what it does,
in the same form as c/epd/epd_trace, which test/epd-parity.test.js compares
it with:

    trace.py waveshare-7in5-v2|inky-ac073tc1a|inky-e673 FRAME.pbm...

Each frame is shown the way hardware/epd_bridge.py shows it (the first in
full, later ones as partial refreshes where the panel has them), then the
panel is put to sleep. BUSY always reads as ready.
"""

import importlib
import os
import sys
import time
import types
import warnings

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
pending = bytearray()


def flush():
    if not pending:
        return
    if len(pending) <= 32:
        body = pending.hex()
    else:
        h = 0xCBF29CE484222325
        for b in pending:
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        body = f"#{h:016x}"
    print(f"spi n={len(pending)} {body}")
    pending.clear()


def spi(data):
    pending.extend(b & 0xFF for b in data)  # Waveshare's driver sends ~byte


def pin(name, level):
    flush()
    print(f"pin {name} {int(bool(level))}")


def delay(ms):
    flush()
    print(f"delay {ms}")


time.sleep = lambda s: delay(round(s * 1000))
warnings.simplefilter("ignore")


def package(name):
    mod = types.ModuleType(name)
    mod.__path__ = [os.path.join(HERE, name)]
    sys.modules[name] = mod
    return mod


def waveshare(frames):
    cfg = types.ModuleType("waveshare_epd.epdconfig")
    cfg.RST_PIN, cfg.DC_PIN, cfg.CS_PIN, cfg.BUSY_PIN, cfg.PWR_PIN = 17, 25, 8, 24, 18
    names = {17: "RST", 25: "DC", 8: "CS", 18: "PWR"}
    cfg.digital_write = lambda p, v: pin(names[p], v)
    cfg.digital_read = lambda p: 1
    cfg.delay_ms = lambda ms: delay(int(ms))
    cfg.spi_writebyte = spi
    cfg.spi_writebyte2 = spi
    cfg.SPI = types.SimpleNamespace(writebytes=spi, writebytes2=spi)

    def module_init(cleanup=False):
        pin("PWR", 1)
        return 0

    def module_exit(cleanup=False):
        pin("RST", 0)
        pin("DC", 0)
        pin("PWR", 0)

    cfg.module_init, cfg.module_exit = module_init, module_exit
    package("waveshare_epd")
    sys.modules["waveshare_epd.epdconfig"] = cfg
    epd = importlib.import_module("waveshare_epd.epd7in5_V2").EPD()

    epd.init()
    epd.display(epd.getbuffer(frames[0]))
    partial = False
    for f in frames[1:]:
        if not partial:
            epd.init_part()
            partial = True
        epd.display_Partial(epd.getbuffer(f), 0, 0, epd.width, epd.height)
    epd.sleep()


def inky(module, frames):
    class Value:
        ACTIVE = "active"
        INACTIVE = "inactive"

    line = types.ModuleType("gpiod.line")
    line.Value = Value
    line.Bias = line.Direction = line.Edge = types.SimpleNamespace()
    gpiod = types.ModuleType("gpiod")
    gpiod.line = line
    gpiod.LineSettings = dict
    sys.modules.update({"gpiod": gpiod, "gpiod.line": line, "gpiodevice": types.ModuleType("gpiodevice")})

    class Gpio:
        names = {8: "CS", 22: "DC", 27: "RST"}

        def set_value(self, p, v):
            pin(self.names[p], v == Value.ACTIVE)

        def get_value(self, p):
            return Value.ACTIVE  # BUSY high: ready

    class Spi:
        max_speed_hz = 0
        no_cs = False

        def open(self, bus, dev):
            pass

        def xfer(self, values):
            spi(values)

        xfer3 = xfer

    class I2c:
        def write_i2c_block_data(self, *args):
            raise OSError("no EEPROM")

    package("inky")
    cls = importlib.import_module(f"inky.{module}").Inky
    display = cls(resolution=(800, 480), gpio=Gpio(), spi_bus=Spi(), i2c_bus=I2c())
    for f in frames:
        # As hardware/epd_bridge.py does it.
        pal = Image.new("P", f.size)
        pal.putdata([display.WHITE if v else display.BLACK for v in f.getdata()])
        display.set_image(pal)
        display.show()


def main():
    panel, paths = sys.argv[1], sys.argv[2:]
    frames = [Image.open(p).convert("1") for p in paths]
    if panel == "waveshare-7in5-v2":
        waveshare(frames)
    elif panel == "inky-ac073tc1a":
        inky("inky_ac073tc1a", frames)
    elif panel == "inky-e673":
        inky("inky_e673", frames)
    else:
        sys.exit(f"unknown panel {panel}")
    flush()


if __name__ == "__main__":
    main()
