#!/usr/bin/env python3
"""The factory test (factory.c) on the emulator (tools/emu/headless.py),
started over USB as on a freshly flashed badge: each step's name, LED and
ROW going on by themselves, KEY failing without all four buttons and B
trying it again, USB (the emulator's USB power and charger switched on and
off), IR on its own echo, OK, then the badge again. Holding A + B leaves
the test. USB packets get no reply while USB power is out. Then the
power-on: the emulator skips the test, unless asked as the badge does
(factory) or on a fresh start, which erases the persistent record and
keeps the items. The test runs at full brightness and puts the badge's
own back after it, also when the power goes during the test.

  python3 tools/factory_test/emu_factory_test.py
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

FACTORY = 0x52
NAME_GET = 0x30
TYPE_APP = 2
APP = os.path.join(ROOT, "webui", "apps", "ripple.hhapp")
failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return False


def lit(emu):
    return sum(1 for v in emu.wait_frames() if v)


def logged(emu, text):
    return any(text in line for line in emu.log)


def label(frame, text, y):
    """The brightness of text, centred at row y (7 rows: no descenders), or 0
    if the grid does not show it there. Only the rows of text are looked at."""
    want = text_columns(text)
    x0 = (GRID_W - len(want)) // 2
    top = 0
    for x in range(GRID_W):
        col = want[x - x0] if 0 <= x - x0 < len(want) else 0
        for dy in range(7):
            slot = GRID[y + dy][x] if y + dy < GRID_H else None
            if slot is None:
                continue
            if bool(frame[slot]) != bool(col >> dy & 1):
                return 0
            top = max(top, frame[slot])
    return top


def frames(e, seconds):
    """The LEDs every 10 ms for a while"""
    seen, end = [], time.time() + seconds
    while time.time() < end:
        seen.append(e.wait_frames() if e.frame is None else e.frame[1:])
        time.sleep(0.01)
    return seen


def chg(frame):
    return label(frame, "CHG", 9) > 0  # factory.c: CHG under USB while the battery charges


def usb_step(e):
    """The USB step, with the emulator's USB power (PC3) and charger (PB22)"""
    check(e.pins() == (False, True), "no USB power: PC3 low, and the charger off: CHRG (PB22) high")
    seen = frames(e, 2.0)  # USB out since before the step, and the battery charges
    check(any(0 < label(f, "USB", 2) <= 30 for f in seen), "no USB power: USB blinks")
    check(not any(chg(f) for f in seen), "no USB power: no CHG, though the battery would charge")
    check(not logged(e, "factory IR"), "no USB power: the step waits")

    # (the waits leave room for a firmware that wants CHRG steady for 500 ms)
    e.usb_power(True)
    check(e.pins() == (True, False), "USB power in: PC3 high, CHRG low")
    check(wait_for(lambda: chg(e.frame[1:]), 0.9), "USB power in: CHG")
    check(label(e.frame[1:], "USB", 2) == 255, "USB power in: USB stays lit")
    e.usb_power(False)  # out again within the second it needs
    check(wait_for(lambda: not chg(e.frame[1:]), 0.8), "USB power out: CHG goes")
    time.sleep(1.0)
    check(not logged(e, "factory IR"), "less than a second of USB power is not enough")

    e.usb_power(True)
    e.charging(False)
    check(wait_for(lambda: label(e.frame[1:], "USB", 2) == 255, 0.6) and not chg(e.frame[1:]),
          "USB power in, the battery full: USB, no CHG")
    e.no_battery()  # CHRG toggles every 100 ms
    high = []
    for _ in range(30):
        high.append(e.pins()[1])
        time.sleep(0.02)
    check(any(high) and not all(high), "no battery: CHRG toggles (high in %d of %d samples)" % (sum(high), len(high)))
    seen = frames(e, 0.6)
    check(not all(chg(f) for f in seen), "no battery: CHG does not stay on (%d of %d frames)" % (sum(map(chg, seen)), len(seen)))
    e.charging(False)  # a battery again, for the tests after this one
    check(wait_for(lambda: logged(e, "factory IR"), 2), "USB power for a second: IR")


def brightness(emu):
    return emu.usb(QUERY)[1]


def run(e):
    e.wait_frames()
    e.set_name("ALICE")
    check(brightness(e) == 32, "a new badge: brightness 32")
    e.usb(FACTORY)
    check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "USB 0x52 starts it: LED")
    check(brightness(e) == 255, "the test runs at full brightness")
    time.sleep(1.2)
    check(lit(e) >= 370, "LED: every LED on (%d lit)" % lit(e))
    e.buttons(BTN_A)  # A held: no menu, and LED goes on by itself
    time.sleep(1.2)
    e.buttons(0)
    check(not logged(e, "factory FAIL"), "A does nothing in LED (and opens no menu)")
    check(wait_for(lambda: logged(e, "factory ROW"), 5), "LED goes on to ROW by itself")
    time.sleep(1.3)
    n = lit(e)
    check(n in (16, 24), "ROW: one line at a time (%d lit)" % n)
    check(wait_for(lambda: logged(e, "factory KEY"), 7), "ROW goes on to KEY by itself")
    time.sleep(1.2)
    for b in (BTN_C, BTN_A, BTN_D):
        e.press(b)
    check(not logged(e, "factory USB"), "three of four buttons: not yet")
    check(wait_for(lambda: logged(e, "factory FAIL KEY: missing: . B . ."), 22), "20 s without B: KEY fails, B missing")
    e.press(BTN_B)
    check(wait_for(lambda: shows_still(e.wait_frames(), "KEY"), 1), "B tries KEY again")
    time.sleep(1.2)
    e.usb_power(False)  # unplugged before the USB step, and the battery would charge
    e.charging(True)
    for b in (BTN_A, BTN_B, BTN_C, BTN_D):
        e.press(b)
    check(wait_for(lambda: logged(e, "factory USB"), 1), "all four: USB")
    time.sleep(1.1)  # the step's name
    usb_step(e)
    check(wait_for(lambda: logged(e, "factory PASS"), 4), "IR hears its own echo: PASS")
    check(b"T\x04TEST" in e.ir_sent, "it sent test packets: %r" % e.ir_sent[:1])
    check(wait_for(lambda: shows_still(e.wait_frames(), "OK"), 1), "OK")
    check(wait_for(lambda: scroll_offset(e.wait_frames(), "ALICE") is not None or shows_still(e.wait_frames(), "ALICE"), 6),
          "then the name tag")
    check(brightness(e) == 32, "and the badge's own brightness again: 32")

    # Leaving: A + B held for 3 s
    e.usb(FACTORY)
    check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "again over USB")
    e.buttons(BTN_A | BTN_B)
    time.sleep(3.4)
    e.buttons(0)
    check(wait_for(lambda: logged(e, "factory left"), 1), "A + B held 3 s leaves it")
    check(wait_for(lambda: scroll_offset(e.wait_frames(), "ALICE") is not None or shows_still(e.wait_frames(), "ALICE"), 3),
          "back to the name tag")
    check(brightness(e) == 32, "at the badge's own brightness")

    # The power goes during the test: the full brightness was never kept
    e.usb(FACTORY)
    time.sleep(1.5)  # the persistent record is written 1 s after a change


def unplugged(path):
    """USB packets while USB power is out: dropped, as on the badge"""
    e = Emu(path)
    try:
        e.wait_frames()
        e.usb_power(False)
        e.send("u %02x" % QUERY)
        time.sleep(0.5)
        check(e.replies.empty(), "no USB power: a USB packet gets no reply")
        check(logged(e, "USB is unplugged: packet dropped"), "no USB power: the emulator logs the dropped packet")
        e.usb_power(True)
        check(len(e.usb(QUERY)) > 0, "USB power in: the badge answers again")
    finally:
        e.stop()


def power_on(path, **kw):
    """The factory test's first step at power-on, or nothing for 1.5 s;
    returns the emulator and whether the test started"""
    e = Emu(path, **kw)
    started = wait_for(lambda: logged(e, "factory LED"), 1.5) and wait_for(lambda: shows_still(e.wait_frames(), "LED"), 1)
    return e, started


def fresh(path):
    """The power-on, on the flash run() left: the factory test passed, the
    name ALICE, and an item stored here"""
    e, started = power_on(path)
    check(not started, "the emulator skips the factory test at power-on")
    check(e.store(TYPE_APP, "ripple", open(APP, "rb").read())[2] == 0, "an item stored")
    e.stop()

    e, started = power_on(path, factory=True)
    check(not started, "factory: not after it passed")
    e.stop()

    e, started = power_on(path, fresh=True)
    check(started, "fresh: the factory test at power-on")
    name = e.usb(NAME_GET)
    check(name[8:].rstrip(b"\0") == b"", "fresh: the record is erased, so the name too: %r" % name[8:].rstrip(b"\0"))
    check(name[3] == 1, "fresh: the items stay (%d stored)" % name[3])
    e.buttons(BTN_A | BTN_B)  # leave it, not passed
    time.sleep(3.4)
    e.buttons(0)
    check(wait_for(lambda: logged(e, "factory left"), 1), "left without a pass")
    time.sleep(1.5)  # the record is written 1 s after the last change
    e.stop()

    e, started = power_on(path, factory=True)
    check(started, "factory: again at the next power-on, as it has not passed")
    e.stop()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "a.bin")
        e = Emu(path)
        try:
            run(e)
        finally:
            e.stop()
        e = Emu(path)
        try:
            e.wait_frames()
            check(brightness(e) == 32, "power cut during the test, then on: brightness 32, not 255")
        finally:
            e.stop()
        unplugged(path)
        fresh(path)
    print("%s  factory test: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


main()
