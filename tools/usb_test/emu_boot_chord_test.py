#!/usr/bin/env python3
"""Hold C + D for 3 s: the badge shows an arrow running down the grid for a
second, then goes into the USB boot ROM (ledmatrix.c), on the emulator
(tools/emu/headless.py). It works on the name tag, in the menu, in an app
and in the factory test. C + D held for 2 s, and C or D alone, do nothing.
The chord does not change the brightness: the press of D that starts it
would otherwise make the LEDs one step darker, and the boot ROM keeps that.

  python3 tools/usb_test/emu_boot_chord_test.py
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

FACTORY = 0x52
failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def arrow(shift):
    """The slots the arrow lights, as ledmatrix.c draws it"""
    lit = set()
    for y in range(13):
        half = 1 if y < 9 else 12 - y
        for x in range(GRID_W // 2 - half, GRID_W // 2 + half + 1):
            if GRID[y + shift][x] is not None:
                lit.add(GRID[y + shift][x])
    return lit


ARROWS = [arrow(s) for s in range(4)]


def hold_chord(e, seconds):
    """Holds C + D; returns the frames seen and how long until the boot ROM
    (None: it did not go)"""
    start = time.time()
    e.buttons(BTN_C | BTN_D)
    frames, last = [], None
    while time.time() - start < seconds and e.boot_rom is None:
        if e.frame is not last:
            last = e.frame
            frames.append({i for i, v in enumerate(last[1:]) if v})
        time.sleep(0.005)
    e.buttons(0)
    return frames, (e.boot_rom - start if e.boot_rom else None)


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


def shows(e, text):
    """The text on the grid, standing still or scrolling"""
    frame = e.wait_frames()
    return shows_still(frame, text) or scroll_offset(frame, text) is not None


def brightness(e):
    return e.usb(QUERY)[1]


def goes_to_boot_rom(e, where, flash, d_first=False):
    """Holds C + D until the boot ROM. Then starts the badge again from the
    same flash and checks that the brightness from before is still there."""
    before = brightness(e)
    if d_first:  # D first, then C: that press of D makes the LEDs darker
        e.buttons(BTN_D)
        time.sleep(0.3)
        check(brightness(e) < before, "%s: D first, alone, makes the LEDs darker" % where)
    frames, after = hold_chord(e, 6)
    check(after is not None and 3.8 < after < 5, "%s: C + D held 3 s, the boot ROM after the arrow (%s s)" %
          (where, "%.2f" % after if after else "never"))
    shown = [s for s in range(4) if ARROWS[s] in frames]
    check(shown == [0, 1, 2, 3], "%s: the arrow runs down the grid, 4 rows (seen at rows %s)" % (where, shown))
    e.stop()
    again = Emu(flash)
    try:
        again.wait_frames()
        now = brightness(again)
        check(now == before, "%s: the brightness after the boot ROM is as before (%d, was %d)" % (where, now, before))
    finally:
        again.stop()


def flash_of(tmp, name):
    return os.path.join(tmp, name + ".bin")


def fresh(tmp, name):
    e = Emu(flash_of(tmp, name))
    e.wait_frames()
    return e


def main():
    with tempfile.TemporaryDirectory() as tmp:
        # Name tag: 2 s is not enough, C or D alone does nothing, 3 s is
        e = fresh(tmp, "nametag")
        try:
            before = brightness(e)
            frames, after = hold_chord(e, 2)
            time.sleep(0.5)
            check(after is None and e.p.poll() is None, "C + D held 2 s: no boot ROM")
            check(brightness(e) == before, "C + D held 2 s: the brightness as before")
            for b in (BTN_C, BTN_D):
                e.buttons(b)
                time.sleep(3.3)
                e.buttons(0)
            check(e.boot_rom is None and e.usb(QUERY)[0] == 4, "C alone and D alone for 3 s: still the name tag")
            goes_to_boot_rom(e, "name tag", flash_of(tmp, "nametag"))
        finally:
            e.stop()

        # Name tag again, D pressed a moment before C
        e = fresh(tmp, "d_first")
        try:
            goes_to_boot_rom(e, "name tag, D first", flash_of(tmp, "d_first"), d_first=True)
        finally:
            e.stop()

        # The menu: open it with A, then C + D
        e = fresh(tmp, "menu")
        try:
            e.buttons(BTN_A)
            time.sleep(1.0)
            e.buttons(0)
            check(wait_for(lambda: shows(e, "NAME TAG"), 2), "A held: the menu, at NAME TAG")
            goes_to_boot_rom(e, "menu", flash_of(tmp, "menu"))
        finally:
            e.stop()

        # An app that uses C and D itself (Breakout moves its paddle with them)
        e = fresh(tmp, "app")
        try:
            app = open(os.path.join(ROOT, "webui", "apps", "breakout.hhapp"), "rb").read()
            check(e.store(2, "BREAKOUT", app)[0] == 3, "Breakout runs")
            time.sleep(0.5)
            goes_to_boot_rom(e, "app", flash_of(tmp, "app"))
        finally:
            e.stop()

        # The factory test
        e = fresh(tmp, "factory")
        try:
            e.usb(FACTORY)
            check(wait_for(lambda: shows(e, "LED"), 2), "USB 0x52: the factory test, at LED")
            goes_to_boot_rom(e, "factory test", flash_of(tmp, "factory"))
        finally:
            e.stop()

    print("%s  boot ROM chord: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


main()
