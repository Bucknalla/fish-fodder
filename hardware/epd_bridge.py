#!/usr/bin/env python3
"""Drive an e-paper panel for fish-fodder.

fish-fodder renders frames in Node; this small helper pushes them to the panel
using the vendor's Python library. It is started by `fish-fodder run` and talks
JSON lines over stdin/stdout:

  -> {"ready": true, "width": 800, "height": 480, "partial": true}
  <- {"cmd": "show", "path": "/tmp/.../frame.png", "mode": "full" | "partial"}
  -> {"ok": true}
  <- {"cmd": "sleep"}
  -> {"ok": true}

Errors come back as {"error": "..."}.

Drivers:
  waveshare:<module>   a module from Waveshare's `waveshare_epd` package,
                       e.g. waveshare:epd7in5_V2 or waveshare:epd4in2_V2
  inky                 Pimoroni Inky (pHAT / wHAT / Impression), auto-detected
  mock                 no hardware; logs to stderr (for testing)
"""

import argparse
import importlib
import inspect
import json
import signal
import sys

# Vendor libraries like to print(); keep that off the protocol channel.
PROTO = sys.stdout
sys.stdout = sys.stderr


def send(**msg):
    PROTO.write(json.dumps(msg) + "\n")
    PROTO.flush()


def load(path):
    from PIL import Image

    return Image.open(path).convert("1")


class Mock:
    partial = True

    def __init__(self, width=800, height=480):
        self.width, self.height = width, height

    def show(self, img, mode):
        print(f"[mock] {mode} refresh, {img.size[0]}x{img.size[1]}", file=sys.stderr)

    def sleep(self):
        print("[mock] sleep", file=sys.stderr)


class Waveshare:
    """Waveshare panels via their `waveshare_epd` Python package.

    Full refreshes follow Waveshare's examples: init(), display(), sleep().

    Partial refresh is used on panels whose driver has init_part() and
    display_Partial(image, x0, y0, x1, y1) (the 7.5" V2 family). As in
    Waveshare's own clock demo, the panel stays in partial mode between
    minute updates, and every new fish brings a full refresh, which clears
    any ghosting. Other panels always get full refreshes.
    """

    def __init__(self, module):
        epd_module = importlib.import_module(f"waveshare_epd.{module}")
        self.epd = epd_module.EPD()
        # Report landscape; getbuffer() rotates landscape images for panels
        # that are natively portrait.
        self.width = max(self.epd.width, self.epd.height)
        self.height = min(self.epd.width, self.epd.height)
        fn = getattr(self.epd, "display_Partial", None)
        self.partial = (
            hasattr(self.epd, "init_part")
            and fn is not None
            and len(inspect.signature(fn).parameters) == 5
        )
        self.in_partial_mode = False

    def show(self, img, mode):
        buf = self.epd.getbuffer(img)
        if mode == "partial" and self.partial:
            if not self.in_partial_mode:
                self.epd.init_part()
                self.in_partial_mode = True
            self.epd.display_Partial(buf, 0, 0, self.epd.width, self.epd.height)
            return
        self.epd.init()
        self.epd.display(buf)
        if self.partial:
            # Stay awake: the next minute will be a partial refresh.
            self.epd.init_part()
            self.in_partial_mode = True
        else:
            self.epd.sleep()
            self.in_partial_mode = False

    def sleep(self):
        self.epd.sleep()
        self.in_partial_mode = False


class Inky:
    """Pimoroni Inky boards. They have no partial refresh."""

    partial = False

    def __init__(self):
        from inky.auto import auto

        self.display = auto()
        self.width, self.height = self.display.resolution

    def show(self, img, mode):
        from PIL import Image

        if img.size != (self.width, self.height):
            img = img.resize((self.width, self.height))
        # Inky boards take palette indices; BLACK/WHITE differ between models.
        black, white = self.display.BLACK, self.display.WHITE
        pal = Image.new("P", img.size)
        pal.putdata([white if v else black for v in img.getdata()])
        self.display.set_image(pal)
        self.display.show()

    def sleep(self):
        pass


def open_driver(name):
    kind, _, arg = name.partition(":")
    if kind == "waveshare":
        if not arg:
            raise ValueError("waveshare needs a module, e.g. waveshare:epd7in5_V2")
        return Waveshare(arg)
    if kind == "inky":
        return Inky()
    if kind == "mock":
        w, _, h = (arg or "800x480").partition("x")
        return Mock(int(w), int(h))
    raise ValueError(f"unknown driver {name!r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--driver", required=True)
    args = ap.parse_args()

    try:
        panel = open_driver(args.driver)
    except Exception as e:  # report setup problems over the protocol
        send(error=f"{type(e).__name__}: {e}")
        return 1
    send(ready=True, width=panel.width, height=panel.height, partial=panel.partial)

    # Ctrl-C and `systemctl stop` signal the whole process group. Let the Node
    # side shut us down (it asks for "sleep", then closes stdin) rather than
    # dying mid-refresh.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    asleep = False

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
            if msg["cmd"] == "show":
                panel.show(load(msg["path"]), msg.get("mode", "full"))
                asleep = False
            elif msg["cmd"] == "sleep":
                panel.sleep()
                asleep = True
            else:
                raise ValueError(f"unknown cmd {msg['cmd']!r}")
            send(ok=True)
        except Exception as e:
            send(error=f"{type(e).__name__}: {e}")
    # stdin closed: Node has gone. Never leave the panel powered up.
    if not asleep:
        panel.sleep()
    return 0


if __name__ == "__main__":
    sys.exit(main())
