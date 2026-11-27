#!/usr/bin/env python3
"""The brightness on the emulator (tools/emu/headless.py): a badge that
never saved a brightness starts at 32 of 255; the USB command BRIGHTNESS
(0x32) sets 1 .. 255, refuses 0, and the badge keeps the value over a
power cycle. Outside apps, B and D show the level for about a second at
the bottom of the grid, over what runs, which comes back as it was; the
menu's BRIGHTNESS shows the same dots. In an app, B and D are the app's.

  python3 tools/brightness_test/emu_brightness_test.py
"""
import contextlib
import io
import os
import runpy
import sys
import tempfile
import time
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

BRIGHTNESS, CLEAR, FRAME_CHUNK, FRAME_COMMIT, STOP = 0x32, 0x05, 0x02, 0x03, 0x26
MODE_CUSTOM, MODE_APP, MODE_NAMETAG = 1, 3, 4
TYPE_APP = 2
LEVELS = [8, 16, 32, 64, 128, 255]  # menu.c
LEVEL_Y = 13                        # menu.h MENU_LEVEL_Y
DOTS = [GRID[LEVEL_Y][3 + 2 * i] for i in range(6)]
BAND = [s for y in (LEVEL_Y - 1, LEVEL_Y, LEVEL_Y + 1) for s in GRID[y] if s is not None]
assert None not in DOTS
failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def level_shown(frame):
    """The level the dots show (0 .. 5), or None if the grid's bottom rows
    are not the level: the band dark, the dots bright up to the level and
    dim after it"""
    if any(frame[s] for s in BAND if s not in DOTS):
        return None
    lit = [frame[s] for s in DOTS]
    n = sum(1 for v in lit if v == 255)
    if lit != [255] * n + [40] * (6 - n) or n == 0:
        return None
    return n - 1


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


def brightness(e):
    return e.usb(QUERY)[1]


def full_frame(e):
    """The host draws every LED at 200 (custom mode)"""
    frame = bytes([200]) * (COLS * 16)
    for off in range(0, len(frame), 58):
        e.usb(FRAME_CHUNK, off & 0xFF, off >> 8, *frame[off:off + 58])
    return e.usb(FRAME_COMMIT)


def badge_py(e, *args):
    """Runs tools/badge.py with args, its USB through the emulator (a
    stand-in for pyusb); returns what it printed, and its exit message"""
    class Dev:
        iSerialNumber = 3

        def is_kernel_driver_active(self, i):
            return False

        def write(self, ep, data, timeout=0):
            self.reply = e.usb(*data)

        def read(self, ep, n, timeout=0):
            return self.reply

    usb = types.ModuleType("usb")
    dev = Dev()
    usb.core = types.SimpleNamespace(find=lambda find_all=False, **kw: [dev] if find_all else dev, USBError=OSError)
    usb.util = types.SimpleNamespace(claim_interface=lambda dev, i: None, get_string=lambda dev, i: "EMU")
    saved = {k: sys.modules.get(k) for k in ("usb", "usb.core", "usb.util")}
    sys.modules.update({"usb": usb, "usb.core": usb.core, "usb.util": usb.util})
    argv, sys.argv = sys.argv, ["badge.py"] + list(args)
    out, exit_msg = io.StringIO(), None
    try:
        with contextlib.redirect_stdout(out):
            runpy.run_path(os.path.join(ROOT, "tools", "badge.py"), run_name="__main__")
    except SystemExit as x:
        exit_msg = x.code
    finally:
        sys.argv = argv
        for k, v in saved.items():
            if v is None:
                sys.modules.pop(k, None)
            else:
                sys.modules[k] = v
    return out.getvalue(), exit_msg


def run_fresh(e):
    e.wait_frames()
    check(brightness(e) == 32, "a badge that never saved a brightness starts at 32")
    check(e.frame[0] == 32, "and the LEDs are at 32")

    r = e.usb(BRIGHTNESS, 100)
    check(r[2] == 0 and r[1] == 100, "BRIGHTNESS 100: ok, the reply carries 100")
    time.sleep(0.1)
    check(e.frame[0] == 100, "the LEDs are at 100")
    r = e.usb(BRIGHTNESS, 0)
    check(r[2] == 1 and r[1] == 100, "BRIGHTNESS 0 is refused (result 1), the brightness stays")
    r = e.usb(BRIGHTNESS)
    check(r[2] == 1 and r[1] == 100, "BRIGHTNESS without a value is refused")
    r = e.usb(BRIGHTNESS, 255)
    check(r[2] == 0 and r[1] == 255, "BRIGHTNESS 255: ok")

    # tools/badge.py brightness [N]
    out, msg = badge_py(e, "brightness", "64")
    check("brightness: 64 of 255" in out and brightness(e) == 64, "badge.py brightness 64: set, and printed")
    out, msg = badge_py(e, "brightness")
    check("brightness: 64 of 255" in out and brightness(e) == 64, "badge.py brightness: printed, unchanged")
    out, msg = badge_py(e, "brightness", "0")
    check(msg == "brightness: 1 to 255" and brightness(e) == 64, "badge.py brightness 0: refused before it sends")

    r = e.usb(BRIGHTNESS, 100)
    time.sleep(1.5)  # the persistent record is written 1 s after the last change


def run_kept(e):
    e.wait_frames()
    check(brightness(e) == 100, "after a power cycle the brightness is still 100")

    # On the name tag, B: one level up (128), the dots show level 4 for a
    # second, then the name tag's dark bottom rows come back
    check(level_shown(e.wait_frames()) is None, "the name tag shows no level")
    e.press(BTN_B)
    check(brightness(e) == 128, "B: 128")
    check(level_shown(e.wait_frames()) == 4, "the level shows: 5 dots bright, 1 dim")
    check(wait_for(lambda: not any(e.frame[1:][s] for s in BAND), 1.5), "about a second later it is gone")
    e.press(BTN_D)
    e.press(BTN_D)
    check(brightness(e) == 32, "D twice: 32")
    check(level_shown(e.wait_frames()) == 2, "the level shows 3 dots")

    # Over what the host drew, which never draws again: the frame comes back
    time.sleep(1.2)
    full_frame(e)
    time.sleep(0.2)
    check(all(v == 200 for v in e.wait_frames()), "the host's frame: every LED 200")
    e.press(BTN_B)
    f = e.wait_frames()
    check(level_shown(f) == 3, "B over it: the level shows (4 dots)")
    check(all(f[s] == 200 for s in range(COLS * 16) if s not in BAND), "the rest of the host's frame stays")
    check(wait_for(lambda: all(v == 200 for v in e.frame[1:]), 1.5), "then the whole frame is back")
    check(e.usb(QUERY)[0] == MODE_CUSTOM, "still the host's custom mode")
    for _ in range(3):
        e.press(BTN_B)
    check(brightness(e) == 255, "B three times: 255")
    e.press(BTN_B)
    check(brightness(e) == 255 and level_shown(e.wait_frames()) == 5, "B at the top: stays 255, all 6 dots")
    time.sleep(1.3)
    e.usb(STOP)

    # The menu's BRIGHTNESS: NAME TAG, (no items), EDIT NAME, BRIGHTNESS
    e.usb(BRIGHTNESS, 16)
    e.buttons(BTN_A)
    time.sleep(1.0)
    e.buttons(0)
    time.sleep(0.15)
    for _ in range(2):
        e.press(BTN_D)
    check(level_shown(e.wait_frames()) == 1, "the menu's BRIGHTNESS shows the level (16: 2 dots)")
    e.press(BTN_A)
    check(brightness(e) == 32 and level_shown(e.wait_frames()) == 2, "tap A: 32, 3 dots")
    e.press(BTN_C)  # EDIT NAME: no dots
    check(level_shown(e.wait_frames()) is None, "the entry before it shows no dots")
    e.buttons(BTN_A)
    time.sleep(1.0)
    e.buttons(0)
    time.sleep(0.3)
    check(level_shown(e.wait_frames()) is None, "the menu closed: no dots on the name tag")

    # In an app, B is the app's: the firmware draws no level over it
    app = open(os.path.join(ROOT, "apps", "build", "snake.hhapp"), "rb").read()
    r = e.store(TYPE_APP, "SNAKE", app)
    check(r[2] == 0 and r[0] == MODE_APP, "the snake app runs")
    time.sleep(0.3)
    e.press(BTN_B)
    seen = []
    end = time.time() + 0.6
    while time.time() < end:
        seen.append(level_shown(e.frame[1:]))
        time.sleep(0.02)
    check(all(v is None for v in seen), "B in the app shows no level")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        flash = os.path.join(tmp, "flash.bin")
        for step in (run_fresh, run_kept):
            e = Emu(flash)
            try:
                step(e)
            finally:
                e.stop()
    print("%s  brightness: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
