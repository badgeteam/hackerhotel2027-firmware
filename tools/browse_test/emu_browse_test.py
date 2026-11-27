#!/usr/bin/env python3
"""Browsing without the menu on the emulator (tools/emu/headless.py): a
short tap of C outside apps starts the next stop in the ring of the name
tag, then every animation and text in store order, then the name tag
again. Apps are not in the ring: an app keeps C for itself, so a tap that
started one would end the browsing. From the name tag or the host's
drawing (custom mode) a tap starts the first animation or text; with none
in the store, the name tag stays.

It acts on the release of C. A long press is no tap, and neither is C
with D down at any time during the press (C + D is the boot ROM chord), a
press that began in the menu, or one that ended a greeting. In an app, C
is the app's.

  python3 tools/browse_test/emu_browse_test.py
"""
import os
import subprocess
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


def animation():
    """A one-frame LA04 animation, from the web interface's encoder"""
    js = """
const fs = require('fs'), vm = require('vm'), path = require('path');
const ctx = {}; vm.createContext(ctx);
for (const f of ['layout_data.js', 'compression.js'])
    vm.runInContext(fs.readFileSync(path.join(process.argv[1], f), 'utf8'), ctx);
const frame = new Array(384).fill(0);
for (let i = 0; i < 208; i += 3) frame[i] = 255;
process.stdout.write(Buffer.from(vm.runInContext('compressPattern', ctx)({ fps: 10, frames: [frame] }, { quantBits: 1 })));
"""
    return subprocess.run(["node", "-e", js, os.path.join(ROOT, "webui")], check=True, capture_output=True).stdout


def app(name):
    return open(os.path.join(ROOT, "apps", "build", name + ".hhapp"), "rb").read()


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
    # Apps first, between and last: the case of a badge with mostly apps
    check(e.store(TYPE_APP, "FIRE", app("fire"))[2] == 0, "store FIRE (item 0, an app)")
    check(e.store(TYPE_TEXT, "ONE", text("ONE"))[2] == 0, "store ONE (item 1, a text)")
    check(e.store(TYPE_APP, "NAMETAG", app("nametag"))[2] == 0, "store NAMETAG (item 2, an app)")
    check(e.store(TYPE_ANIMATION, "ANIM", animation())[2] == 0, "store ANIM (item 3, an animation)")
    check(e.store(TYPE_TEXT, "TWO", text("TWO"))[2] == 0, "store TWO (item 4, a text)")
    check(e.store(TYPE_APP, "FIRE2", app("fire"))[2] == 0, "store FIRE2 (item 5, an app)")
    e.usb(STOP)
    time.sleep(0.2)
    check(state(e)[0] == MODE_NAMETAG and shows_still(e.wait_frames(), NAME), "the name tag shows %s" % NAME)

    # The ring: name tag, ONE, ANIM, TWO, name tag; the apps are skipped
    e.buttons(BTN_C)
    time.sleep(0.15)
    check(state(e)[0] == MODE_NAMETAG, "C down: nothing yet (it acts on the release)")
    e.buttons(0)
    time.sleep(0.1)
    check(state(e) == (MODE_ITEM, 1), "C released: ONE, the first text (the FIRE app before it is skipped)")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 3), "from ONE, a tap of C: ANIM (the NAMETAG app is skipped)")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 4), "from ANIM, a tap of C: TWO")
    time.sleep(0.2)
    check(shows_still(e.wait_frames(), "TWO"), "and the grid shows TWO")
    tap_c(e)
    check(state(e)[0] == MODE_NAMETAG, "from TWO, a tap of C: the name tag again (the FIRE2 app is skipped)")
    time.sleep(0.2)
    check(shows_still(e.wait_frames(), NAME), "and the grid shows %s" % NAME)
    tap_c(e)
    check(state(e) == (MODE_ITEM, 1), "a tap of C there: the first text again, ONE")

    # An app keeps C. Leave it as a person does: hold A, then NAME TAG in
    # the menu (it opens on NAMETAG, item 2: C four times goes back past
    # ONE, FIRE and SAY HI to NAME TAG). From there a tap starts the first
    # text, not the item after the app.
    e.usb(ITEM_RUN, 2, 0)
    time.sleep(0.15)
    check(state(e) == (MODE_APP, 2), "the host starts the NAMETAG app")
    tap_c(e)
    check(state(e) == (MODE_APP, 2), "in the app, a tap of C is the app's: NAMETAG still runs")
    menu(e, 1.2)
    for _ in range(4):
        e.press(BTN_C)
    e.press(BTN_A)
    time.sleep(0.15)
    check(state(e)[0] == MODE_NAMETAG, "hold A, NAME TAG in the menu: the name tag")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 1), "a tap of C there: ONE, the first text (not ANIM, the item after the app)")

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
    check(state(e) == (MODE_ITEM, 3), "a plain tap after all that steps again: ANIM")

    # The menu has C while it is open: a press there does not browse after it
    menu(e, 1.0)
    e.buttons(BTN_C)  # the previous entry in the menu
    time.sleep(0.1)
    e.buttons(BTN_C | BTN_A)
    time.sleep(1.0)  # hold A: the menu closes, C still down
    e.buttons(0)
    time.sleep(0.2)
    check(state(e) == (MODE_ITEM, 3), "C pressed in the menu, released after it closed: ANIM stays")

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
    check(state(e) == (MODE_ITEM, 3), "C down outside, up in the menu, the menu closed: ANIM stays")

    # A press that the outside of apps saw start, but an app saw end, is
    # no tap when the badge comes back from the app
    e.buttons(BTN_C)
    time.sleep(0.1)
    e.usb(ITEM_RUN, 0, 0)  # FIRE while C is down
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

    # From the host's drawing: the first animation or text
    check(e.usb(CLEAR)[0] == MODE_CUSTOM, "CLEAR: the host's custom mode")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 1), "a tap of C there: ONE, the first text (not the FIRE app)")


def run_apps_only(e):
    """A store of apps only: C has nothing to browse"""
    e.wait_frames()
    e.set_name(NAME)
    check(e.store(TYPE_APP, "FIRE", app("fire"))[2] == 0, "apps only: store FIRE (item 0)")
    check(e.store(TYPE_APP, "NAMETAG", app("nametag"))[2] == 0, "apps only: store NAMETAG (item 1)")
    e.usb(STOP)
    time.sleep(0.2)
    tap_c(e)
    time.sleep(0.2)
    check(state(e)[0] == MODE_NAMETAG and shows_still(e.wait_frames(), NAME), "apps only: a tap of C on the name tag changes nothing")
    check(e.usb(CLEAR)[0] == MODE_CUSTOM, "apps only: CLEAR, the host's custom mode")
    tap_c(e)
    time.sleep(0.2)
    check(state(e)[0] == MODE_NAMETAG and shows_still(e.wait_frames(), NAME), "apps only: a tap of C there gives the name tag, no app")


def run_unplayable(e):
    """An animation the store takes (it checks the LA04 magic and length)
    but the player cannot start: C skips it, and the items after it stay
    in the ring"""
    e.wait_frames()
    e.set_name(NAME)
    check(e.store(TYPE_TEXT, "ONE", text("ONE"))[2] == 0, "unplayable: store ONE (item 0, a text)")
    check(e.store(TYPE_ANIMATION, "BAD", b"LA04" + bytes(8))[2] == 0, "unplayable: store BAD (item 1, no frames)")
    check(e.store(TYPE_TEXT, "TWO", text("TWO"))[2] == 0, "unplayable: store TWO (item 2, a text)")
    e.usb(STOP)
    time.sleep(0.2)
    tap_c(e)
    check(state(e) == (MODE_ITEM, 0), "unplayable: a tap of C on the name tag: ONE")
    tap_c(e)
    check(state(e) == (MODE_ITEM, 2), "unplayable: from ONE, a tap of C: TWO (BAD cannot start, so it is skipped)")
    tap_c(e)
    check(state(e)[0] == MODE_NAMETAG, "unplayable: from TWO, a tap of C: the name tag")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        for test, flash in ((run, "flash.bin"), (run_apps_only, "apps_only.bin"),
                            (run_unplayable, "unplayable.bin")):
            e = Emu(os.path.join(tmp, flash))
            try:
                test(e)
            finally:
                e.stop()
    print("%s  browse: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
