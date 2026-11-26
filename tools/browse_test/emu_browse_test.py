#!/usr/bin/env python3
"""Browsing without the menu on the emulator (tools/emu/headless.py): a
short tap of C outside apps starts the next stop in the ring of the name
tag, then every store item in store order, apps too, then the name tag
again. On the name tag it goes on after the item that ran last, so a
person who leaves an app (which keeps C) with the menu's NAME TAG goes
on with the item after it. It acts on the release of C. A long press is no tap, and neither
is C with D down at any time during the press (C + D is the boot ROM
chord), a press that began in the menu, or one that ended a greeting. In
an app, C is the app's.

  python3 tools/browse_test/emu_browse_test.py
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

CLEAR, STOP = 0x05, 0x26
MODE_CUSTOM, MODE_ITEM, MODE_APP, MODE_NAMETAG = 1, 2, 3, 4
TYPE_APP = 2
NAME = "BOB"
failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def text(s):
    """A text item (nametag.h): plain, 10 columns a second"""
    return b"HHT1" + bytes([10, 1, 0, 0]) + s.encode()


def state(e):
    """(mode, running item) from the status reply"""
    r = e.usb(QUERY)
    return r[0], r[4]


def tap_c(e, ms=80):
    e.press(BTN_C, ms)
    time.sleep(0.05)


def menu(e, hold=0.8):
    """Holds A until the menu opens (1 s in an app), and lets go"""
    e.buttons(BTN_A)
    time.sleep(hold)
    e.buttons(0)
    time.sleep(0.15)


def run(e):
    e.wait_frames()
    e.set_name(NAME)
    app = open(os.path.join(ROOT, "apps", "build", "fire.hhapp"), "rb").read()
    check(e.store(TYPE_APP, "FIRE", app)[2] == 0, "store FIRE (item 0, an app)")
    check(e.store(TYPE_TEXT, "ONE", text("ONE"))[2] == 0, "store ONE (item 1, a text)")
    check(e.store(TYPE_TEXT, "TWO", text("TWO"))[2] == 0, "store TWO (item 2, a text)")
    e.usb(STOP)
    time.sleep(0.2)
    check(state(e)[0] == MODE_NAMETAG and shows_still(e.wait_frames(), NAME), "the name tag shows %s" % NAME)

    # The ring: name tag, FIRE, ONE, TWO, name tag
    e.buttons(BTN_C)
    time.sleep(0.15)
    check(state(e)[0] == MODE_NAMETAG, "C down: nothing yet (it acts on the release)")
    e.buttons(0)
    time.sleep(0.1)
    check(state(e) == (MODE_APP, 0), "C released: item 0, the FIRE app, runs")
    tap_c(e)
    check(state(e) == (MODE_APP, 0), "in the app, a tap of C is the app's: FIRE still runs")

    # Leave the app as a person does: hold A, then NAME TAG in the menu
    # (it opens on FIRE: C twice goes back past SAY HI to NAME TAG)
    menu(e, 1.2)
    e.press(BTN_C)
    e.press(BTN_C)
    e.press(BTN_A)
    time.sleep(0.15)
    check(state(e)[0] == MODE_NAMETAG, "hold A, NAME TAG in the menu: the name tag")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 1), "a tap of C there: ONE, the item after FIRE")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 2), "from ONE, a tap of C: TWO")
    time.sleep(0.2)
    check(shows_still(e.wait_frames(), "TWO"), "and the grid shows TWO")
    tap_c(e)
    check(state(e)[0] == MODE_NAMETAG, "from the last item, a tap of C: the name tag again")
    time.sleep(0.2)
    check(shows_still(e.wait_frames(), NAME), "and the grid shows %s" % NAME)
    tap_c(e)
    check(state(e) == (MODE_APP, 0), "a tap of C there: the first item again, FIRE")

    # An app that ran last, then the menu's NAME TAG from the name tag
    # itself: the next tap still goes on after that app
    menu(e, 1.2)
    e.press(BTN_C)
    e.press(BTN_C)
    e.press(BTN_A)
    time.sleep(0.15)
    menu(e)
    e.press(BTN_A)  # the menu opens on NAME TAG
    time.sleep(0.15)
    tap_c(e)
    check(state(e) == (MODE_ITEM, 1), "the name tag after FIRE, twice through the menu: a tap gives ONE")

    # Not a tap
    tap_c(e, 700)
    check(state(e) == (MODE_ITEM, 1), "a long press of C (0.7 s): ONE stays")
    e.buttons(BTN_C)
    time.sleep(0.1)
    e.buttons(BTN_C | BTN_D)
    time.sleep(0.1)
    e.buttons(BTN_C)
    time.sleep(0.1)
    e.buttons(0)
    time.sleep(0.15)
    check(state(e) == (MODE_ITEM, 1), "C, then D during it (the chord's start), both up: no step")
    e.buttons(BTN_C | BTN_D)
    time.sleep(0.15)
    e.buttons(0)
    time.sleep(0.15)
    check(state(e) == (MODE_ITEM, 1), "C and D tapped together: no step")
    e.buttons(BTN_D)
    time.sleep(0.1)
    e.buttons(BTN_D | BTN_C)
    time.sleep(0.1)
    e.buttons(BTN_D)
    time.sleep(0.1)
    e.buttons(0)
    time.sleep(0.15)
    check(state(e) == (MODE_ITEM, 1), "D held, C tapped during it: no step")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 2), "a plain tap after all that steps again: TWO")

    # The menu has C while it is open: a press there does not browse after it
    menu(e, 1.0)
    e.buttons(BTN_C)  # the previous entry in the menu
    time.sleep(0.1)
    e.buttons(BTN_C | BTN_A)
    time.sleep(1.0)  # hold A: the menu closes, C still down
    e.buttons(0)
    time.sleep(0.2)
    check(state(e) == (MODE_ITEM, 2), "C pressed in the menu, released after it closed: TWO stays")

    # A short press of C that starts outside, while A is held for the
    # menu, and ends in the menu: no tap when the menu closes
    e.buttons(BTN_A)
    time.sleep(0.4)
    e.buttons(BTN_A | BTN_C)  # C down outside, 0.3 s before the menu opens
    time.sleep(0.5)
    e.buttons(0)  # both up, in the menu
    time.sleep(0.15)
    menu(e, 1.0)  # hold A: the menu closes
    time.sleep(0.1)
    check(state(e) == (MODE_ITEM, 2), "C down outside, up in the menu, the menu closed: TWO stays")

    # A press that the outside of apps saw start, but an app saw end, is
    # no tap when the badge comes back from the app
    e.buttons(BTN_C)
    time.sleep(0.1)
    e.usb(0x25, 0, 0)  # ITEM_RUN FIRE while C is down
    time.sleep(0.1)
    e.buttons(0)
    time.sleep(0.1)
    check(state(e) == (MODE_APP, 0), "C down, the host starts FIRE, C up in the app: FIRE runs")
    e.usb(STOP)
    time.sleep(0.15)
    check(state(e)[0] == MODE_NAMETAG, "STOP: the name tag, and that press of C does not step")

    # A greeting: the tap of C that ends it does nothing else
    e.usb(STOP)
    e.buttons(BTN_A)
    time.sleep(1.0)
    e.buttons(0)
    time.sleep(0.15)
    e.press(BTN_D)  # SAY HI
    e.press(BTN_A)
    time.sleep(0.3)
    check(state(e)[0] == MODE_NAMETAG and not shows_still(e.wait_frames(), NAME), "SAY HI: the greeting shows")
    tap_c(e)
    time.sleep(0.2)
    check(state(e)[0] == MODE_NAMETAG and shows_still(e.wait_frames(), NAME), "a tap of C ends it: the name tag, no step")

    # From the host's drawing: the first item
    check(e.usb(CLEAR)[0] == MODE_CUSTOM, "CLEAR: the host's custom mode")
    tap_c(e)
    check(state(e) == (MODE_APP, 0), "a tap of C there: item 0")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        e = Emu(os.path.join(tmp, "flash.bin"))
        try:
            run(e)
        finally:
            e.stop()
    print("%s  browse: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
