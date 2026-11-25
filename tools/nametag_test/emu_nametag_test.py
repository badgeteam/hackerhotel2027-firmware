#!/usr/bin/env python3
"""Banana shots on the name tag (nametag.c) on the emulator
(tools/emu/headless.py): a shoot sends a ring out and blinks the arms in
the shooter's colour, a heal pulses them green, a set team shows the
team's colour, a text byte shows nothing; a second later the name tag is
as before. A shot that comes in while the menu, the host's custom mode or
an app has the LEDs never shows later on the name tag, also when the app
leaves it in the queue; an app gets its shots itself.

  python3 tools/nametag_test/emu_nametag_test.py
"""
import os
import re
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

CLEAR, STOP = 0x05, 0x26
TYPE_APP, MODE_APP, MODE_NAMETAG = 2, 3, 4
NAME = "BOB"
failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def layout_table(name):
    """A table from layout.c, as a list of tuples of numbers"""
    src = open(os.path.join(ROOT, "layout.c")).read()
    body = src[src.index(name + "["):]
    body = body[body.index("{") + 1:body.index("};")]
    return [tuple(int(v) for v in re.findall(r"\d+", row)) for row in re.findall(r"\{([^{}]*)\}", body)], body


RGB = [t[:3] for t in layout_table("layout_rgb")[0]]  # r, g, b slots of each arm LED
EDGE = [int(v) for v in re.findall(r"\d+", layout_table("layout_edge")[1])]
# Grid LEDs above and below the text: dark on the name tag
OFF_TEXT = [s for y, row in enumerate(GRID) if y < TEXT_Y or y >= TEXT_Y + 8 for s in row if s is not None]
assert len(RGB) == 35 and len(EDGE) == 71 and OFF_TEXT


def arms(frame):
    return [(frame[r], frame[g], frame[b]) for r, g, b in RGB]


def uniform(frame):
    """Every arm LED the same colour: the name tag's arms never are"""
    return len(set(arms(frame))) == 1


def ring(frame):
    """LEDs lit off the text: the grid above and below it, or much of the outline"""
    return any(frame[s] for s in OFF_TEXT) or sum(1 for s in EDGE if frame[s]) >= 15


def colour(frame, want):
    """Every arm LED near want (r, g, b)"""
    return all(all(abs(c - w) <= 40 for c, w in zip(rgb, want)) for rgb in arms(frame))


def frames(emu, seconds):
    """The frames the emulator sends for a while"""
    seen, last, end = [], None, time.time() + seconds
    while time.time() < end:
        f = emu.frame
        if f is not last:
            seen.append(f[1:])
            last = f
        time.sleep(0.005)
    return seen


def plain(emu):
    """The name tag as it is without a shot"""
    f = emu.wait_frames()
    return shows_still(f, NAME) and not ring(f) and not uniform(f)


def quiet(emu, seconds=1.3):
    """No shot shows for a while (from the name tag's first frame on: what
    showed before it may still be in the emulator's last frame)"""
    time.sleep(0.1)
    return not any(ring(f) or uniform(f) for f in frames(emu, seconds))


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return False


def run(e):
    e.wait_frames()
    e.set_name(NAME)
    time.sleep(0.3)
    check(plain(e), "the name tag shows %s, nothing off the text, arms in colours" % NAME)
    check(quiet(e, 1.0), "without a shot no frame has a ring or one colour on all arms")

    # Shoot from team 0 (red): a ring goes out, the arms blink red
    e.shot(0, SHOT_SHOOT)
    seen = frames(e, 1.1)
    check(any(colour(f, (255, 0, 0)) for f in seen), "a shoot from red: the arms turn red")
    check(any(colour(f, (0, 0, 0)) for f in seen), "and blink")
    check(any(any(f[s] for s in OFF_TEXT) for f in seen), "the ring lights the grid above and below the text")
    check(any(sum(1 for s in EDGE if f[s]) >= 15 for f in seen), "and reaches the outline")
    time.sleep(0.4)
    check(plain(e), "after 1.5 s the name tag is as before")
    check(quiet(e, 0.5), "and stays so")

    # Heal: the arms pulse green, whatever the team, and no ring
    e.shot(2, SHOT_HEAL, 3)
    seen = frames(e, 1.1)
    check(any(colour(f, (0, 255, 0)) for f in seen), "a heal: the arms turn green")
    check(len({arms(f)[0] for f in seen if uniform(f)}) >= 4, "and pulse")
    check(not any(ring(f) for f in seen), "with no ring")
    time.sleep(0.4)
    check(plain(e), "then the name tag again")

    # Set team: that team's colour on the arms
    e.shot(4, SHOT_TEAM)
    seen = frames(e, 1.1)
    check(any(colour(f, (255, 0, 200)) for f in seen), "set team 4: the arms turn magenta")
    check(not any(ring(f) for f in seen), "with no ring")
    time.sleep(0.4)
    check(plain(e), "then the name tag again")

    # A text byte: nothing
    e.shot(1, SHOT_BYTE, 1)
    check(quiet(e), "a text byte shows nothing")

    # In the menu: the shot is gone when the name tag comes back
    e.buttons(BTN_A)
    time.sleep(1.0)
    e.shot(0, SHOT_SHOOT)
    time.sleep(0.2)
    e.buttons(0)
    time.sleep(0.15)
    e.press(BTN_A)  # NAME TAG
    check(e.usb(QUERY)[0] == MODE_NAMETAG, "a shot in the menu, then NAME TAG")
    check(quiet(e), "the shot from the menu never shows")
    check(plain(e), "the name tag as before")

    # In the host's custom mode: the same
    check(e.usb(CLEAR)[0] == 1, "CLEAR: custom mode")
    e.shot(0, SHOT_SHOOT)
    e.shot(1, SHOT_HEAL)
    time.sleep(0.3)
    check(e.usb(STOP)[0] == MODE_NAMETAG, "two shots in custom mode, then STOP")
    check(quiet(e), "neither shows on the name tag")
    check(plain(e), "the name tag as before")

    # In an app: the app gets the shot (the banana counts a hit), the name
    # tag never sees it
    app = open(os.path.join(ROOT, "apps", "build", "banana.hhapp"), "rb").read()
    r = e.store(TYPE_APP, "BANANA", app)
    check(r[2] == 0 and r[0] == MODE_APP, "the banana app runs")
    time.sleep(0.5)
    e.shot(0, SHOT_SHOOT)
    check(wait_for(lambda: shows_still(e.wait_frames(), "1"), 2.5), "a shoot in the app: the banana counts 1 hit")
    e.shot(0, SHOT_SHOOT)
    time.sleep(0.1)
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the name tag")
    check(quiet(e), "a shot during the app does not show on the name tag later")
    check(plain(e), "the name tag as before")

    # In an app that does not take shots: they wait in the queue, and still
    # never show on the name tag after it
    app = open(os.path.join(ROOT, "apps", "build", "fire.hhapp"), "rb").read()
    r = e.store(TYPE_APP, "FIRE", app)
    check(r[2] == 0 and r[0] == MODE_APP, "the fire app runs")
    time.sleep(0.5)
    e.shot(0, SHOT_SHOOT)
    e.shot(1, SHOT_HEAL)
    time.sleep(1.5)
    check(e.usb(STOP)[0] == MODE_NAMETAG, "two shots the fire app leaves, then STOP")
    check(quiet(e), "neither shows on the name tag")
    check(plain(e), "the name tag as before")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        e = Emu(os.path.join(tmp, "flash.bin"))
        try:
            run(e)
        finally:
            e.stop()
    print("%s  name tag shots: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
