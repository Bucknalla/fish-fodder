# Vendor e-paper drivers, for comparison

The C drivers in `c/epd/` must send exactly what these send.
`test/epd-parity.test.js` runs both against fake hardware and compares the
byte streams.

- `waveshare_epd/epd7in5_V2.py`: Waveshare's driver for the 7.5" e-Paper V2,
  from [waveshareteam/e-Paper](https://github.com/waveshareteam/e-Paper)
  (`RaspberryPi_JetsonNano/python/lib/waveshare_epd`, commit a794fbc). MIT
  licence, in the file's header.
- `inky/inky_ac073tc1a.py`, `inky/inky_e673.py`, `inky/eeprom.py`: Pimoroni's
  drivers for the Inky Impression 7.3" (7-colour and Spectra 6), from
  [pimoroni/inky](https://github.com/pimoroni/inky). MIT licence
  (`inky/LICENSE`).

`trace.py` loads them with fakes in place of spidev, GPIO and I2C.
