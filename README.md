# fish-fodder

Randomly generate fish with silly names for an e-ink display.

An e-ink clock that catches a new fish every hour. Each hour gets a silly
name (*Lesser Spotted Disco Haddock*, *Captain Brenda Barnaclewick*,
*Wobblichthys bewilderus*), and the name is the seed for
[fishdraw](https://github.com/LingDong-/fishdraw), so the same name always
draws the same fish. About one hour in twenty-five lands a rare catch.

```
┌──────────────────────────────────────────────┐
│ 14:37                              Sat 3 Oct │
│ ──────────────────────────────────────────── │
│                   (a fish)                   │
│                                              │
│             Doodlella puddlensis             │
│  Diet: whatever is in the bottom of the bag  │
└──────────────────────────────────────────────┘
```

The clock redraws every minute with a partial refresh, and does a full refresh
when the new fish arrives on the hour. Panels without partial refresh update
hourly instead.

## Try it without hardware

Needs Node 18 or newer. There are no runtime dependencies.

```sh
npm run sim                 # simulator at http://localhost:8080
```

The simulator runs the same renderer as the device. You can fast-forward
through the hours, jump to any time, switch panel models and orientation, and
see the day's catch. It also mimics the e-ink full-refresh flash.

To get a single HTML file that opens straight from disk:

```sh
npm install && npm run build   # → dist/simulator.html
```

From the command line:

```sh
node bin/fish-fodder.js names                      # the next 24 hours of fish
node bin/fish-fodder.js render --out frame.png     # this minute's frame
node bin/fish-fodder.js render --at 2026-12-25T09:00 --preset inky-what --clock 12h
node bin/fish-fodder.js render --name "Biggus fishus"
node bin/fish-fodder.js run --driver mock          # the clock loop, logging refreshes
node bin/fish-fodder.js --help
```

## On a Raspberry Pi

Any Pi that runs Node works. A Zero 2 W is plenty: a fish takes well under a
second to draw on a laptop, and the next fish is drawn a minute ahead of time.

1. Install Node 18+ and clone this repo.
2. Install your panel's Python library, ideally in a venv in the repo
   (`python3 -m venv .venv`) with Pillow:
   - **Waveshare**: enable SPI (`raspi-config`), then
     `pip install pillow RPi.GPIO spidev gpiozero` and install `waveshare_epd`
     from [Waveshare's e-Paper repo](https://github.com/waveshareteam/e-Paper)
     (`RaspberryPi_JetsonNano/python`, `pip install .`).
   - **Pimoroni Inky**: `pip install inky[rpi] pillow`.
3. Run it:

   ```sh
   node bin/fish-fodder.js run --driver waveshare:epd7in5_V2 --python .venv/bin/python
   node bin/fish-fodder.js run --driver inky --python .venv/bin/python
   ```

   The panel's resolution is detected. `--driver waveshare:<module>` takes any
   module name from `waveshare_epd`, such as `epd4in2_V2` or `epd2in9_V2`.
4. To start it on boot, use [`hardware/fish-fodder.service`](hardware/fish-fodder.service).

### Refreshing

| Panel | Updates | Notes |
| --- | --- | --- |
| Waveshare 7.5" V2 family (`init_part` + `display_Partial`) | every minute | Partial refresh each minute, full refresh on the hour. The panel stays in partial mode between minutes, like Waveshare's clock demo. |
| Other Waveshare panels | hourly | Full refresh, then the panel sleeps. |
| Inky (pHAT, wHAT, Impression) | hourly | No partial refresh. Impression boards take about 30 s per refresh. |

`--update minute` or `--update hour` overrides this. In hourly mode the time
shows the hour the fish arrived (`14:00`).

### Other displays

- `--driver file --out /path/frame.png` writes a 1-bit PNG on every update.
- `--driver exec --exec "my-display-tool {path} {mode}"` writes the PNG and runs
  a command. `{mode}` is `full` or `partial`.
- `fish-fodder serve` also serves `/frame.png` (with optional `?width=`, `?height=`,
  `?clock=12h`, `?salt=`, `?at=`), for ESP32-style frames that fetch an image.

### Options

| Option | |
| --- | --- |
| `--width`, `--height`, `--preset` | Frame size. `run` uses the panel's own size by default. |
| `--rotate 90` | For frames hung in portrait, or upside down (`180`). |
| `--clock 12h` | 12-hour clock with am/pm. |
| `--salt kitchen` | A different fish schedule, so two frames don't match. |

## How it works

- `src/names.js` generates a name and a field note from a seed. Four styles:
  mock-Latin binomials, field-guide common names, characters, and rare legends.
- `src/schedule.js` seeds each catch from the local hour (`2026-10-03T14`, plus
  the optional salt), so every frame and the simulator agree.
- `src/vendor/fishdraw.js` is fishdraw, converted to an ES module. The header
  lists the small changes. The main one: upstream builds its noise table on the
  first draw only, so in a long-running process the same name could draw a
  different fish depending on what came before. fish-fodder resets it, so
  every draw matches `node fishdraw.js --seed "<name>"`.
- `src/render.js` lays out the frame and rasterises it straight to 1 bit, with
  lettering in the Hershey stroke fonts to match fishdraw's plotter style.
- `src/node/` holds the PNG encoder, clock loop, display backends and server.
  `hardware/epd_bridge.py` pushes frames to the panel.

```sh
npm test
```

## Credits

- Fish drawings: [fishdraw](https://github.com/LingDong-/fishdraw) by Lingdong
  Huang, MIT licence (`src/vendor/LICENSE-fishdraw`).
- Lettering: the Hershey Fonts were originally created by Dr. A. V. Hershey
  while working at the U. S. National Bureau of Standards. The format of the
  font data was originally created by James Hurt, Cognition, Inc.
