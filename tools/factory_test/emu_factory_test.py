#!/usr/bin/env python3
"""The factory test (factory.c) on the emulator (tools/emu/headless.py),
started over USB as on a freshly flashed badge: each step's name, LED and
ROW going on by themselves, KEY failing without all four buttons and B
trying it again, USB, IR on its own echo, OK, then the badge again.
Holding A + B leaves the test.

  python3 tools/factory_test/emu_factory_test.py
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


def run(e):
    e.wait_frames()
    e.set_name("ALICE")
    e.usb(FACTORY)
    check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "USB 0x52 starts it: LED")
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
    for b in (BTN_A, BTN_B, BTN_C, BTN_D):
        e.press(b)
    check(wait_for(lambda: logged(e, "factory USB"), 1), "all four: USB")
    check(wait_for(lambda: logged(e, "factory IR"), 4), "USB power for a second: IR")
    check(wait_for(lambda: logged(e, "factory PASS"), 4), "IR hears its own echo: PASS")
    check(b"T\x04TEST" in e.ir_sent, "it sent test packets: %r" % e.ir_sent[:1])
    check(wait_for(lambda: shows_still(e.wait_frames(), "OK"), 1), "OK")
    check(wait_for(lambda: scroll_offset(e.wait_frames(), "ALICE") is not None or shows_still(e.wait_frames(), "ALICE"), 6),
          "then the name tag")

    # Leaving: A + B held for 3 s
    e.usb(FACTORY)
    check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "again over USB")
    e.buttons(BTN_A | BTN_B)
    time.sleep(3.4)
    e.buttons(0)
    check(wait_for(lambda: logged(e, "factory left"), 1), "A + B held 3 s leaves it")
    check(wait_for(lambda: scroll_offset(e.wait_frames(), "ALICE") is not None or shows_still(e.wait_frames(), "ALICE"), 3),
          "back to the name tag")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        e = Emu(os.path.join(tmp, "a.bin"))
        try:
            run(e)
        finally:
            e.stop()
    print("%s  factory test: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


main()
