#!/usr/bin/env python3
"""The name swap (greet.c) on two emulated badges linked by infrared
(tools/emu/headless.py): SAY HI from the menu, the packets on air, what
both badges show, no answer, an empty name, a button that ends it.

  python3 tools/ir_test/emu_greet_test.py
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def say_hi(emu):
    """Hold A for the menu, D to SAY HI (the entry after NAME TAG), tap A"""
    emu.buttons(BTN_A)
    time.sleep(1.0)
    emu.buttons(0)
    time.sleep(0.15)
    emu.press(BTN_D)
    emu.press(BTN_A)


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return False


def shows(emu, text):
    """The grid shows text, still and centred or scrolling"""
    frame = emu.wait_frames()
    return shows_still(frame, text) or scroll_offset(frame, text) is not None


def main():
    with tempfile.TemporaryDirectory() as tmp:
        a, b = Emu(os.path.join(tmp, "a.bin")), Emu(os.path.join(tmp, "b.bin"))
        try:
            run(a, b)
        finally:
            a.stop()
            b.stop()
    print("%s  name swap: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


def run(a, b):
    a.wait_frames()
    b.wait_frames()
    a.set_name("ALICE")
    b.set_name("BOB")
    link(a, b)

    # SAY HI on A: HELLO with its name, B answers, both show the other's name
    say_hi(a)
    check(wait_for(lambda: a.ir_sent, 2), "SAY HI sends a packet")
    check(a.ir_sent[:1] == [b"H\x05ALICE"], "it is HELLO with the name: %r" % a.ir_sent[:1])
    check(wait_for(lambda: b.ir_sent, 2), "the other badge answers")
    check(b.ir_sent[:1] == [b"R\x03BOB"], "with REPLY and its own name: %r" % b.ir_sent[:1])
    check(wait_for(lambda: shows(a, "HI BOB"), 3), "A shows HI BOB")
    check(shows(b, "HI ALICE"), "B shows HI ALICE")
    time.sleep(2)
    check(len(a.ir_sent) == 1, "one HELLO is enough once the REPLY is in (%d sent)" % len(a.ir_sent))

    # B, C or D ends the greeting: B shows its name tag again
    b.press(BTN_C)
    check(wait_for(lambda: shows(b, "BOB"), 2), "C ends the greeting: B shows its name tag")
    # A's ends by itself: "HI BOB" scrolls through once (4.8 s), at least 5 s
    check(wait_for(lambda: shows(a, "ALICE"), 6), "A's greeting ends by itself, the name tag comes back")

    # Both say hi at once: each takes the other's HELLO as the answer
    a.ir_sent.clear()
    b.ir_sent.clear()
    say_hi(a)
    say_hi(b)
    check(wait_for(lambda: shows(a, "HI BOB") and shows(b, "HI ALICE"), 4), "both say hi at once: both greet")
    a.press(BTN_D)
    b.press(BTN_D)

    # An empty name
    b.set_name("")
    a.ir_sent.clear()
    b.ir_sent.clear()
    say_hi(a)
    check(wait_for(lambda: b.ir_sent, 3) and b.ir_sent[0] == b"R\x00", "an empty name goes out empty: %r" % b.ir_sent[:1])
    check(wait_for(lambda: shows(a, "HI!"), 2), "and shows as HI!")
    a.press(BTN_D)
    b.press(BTN_D)
    b.set_name("BOB")

    # No answer: 4 HELLOs, 1.5 s apart, then NO REPLY
    a.peers.clear()
    b.peers.clear()
    a.ir_sent.clear()
    b.ir_sent.clear()
    t0 = time.time()
    say_hi(a)
    check(wait_for(lambda: shows(a, "NO REPLY"), 9), "without an answer A shows NO REPLY")
    check(len(a.ir_sent) == 4, "after 4 HELLOs (%d sent, %.1f s)" % (len(a.ir_sent), time.time() - t0))
    check(not b.ir_sent, "B heard nothing")
    check(wait_for(lambda: shows(a, "ALICE"), 5), "then the name tag again")

    # The menu still runs items: the first item is now after SAY HI
    payload = b"HHT1" + bytes([10, 1, 0, 0]) + b"Hey"
    r = a.store(TYPE_TEXT, "hey", payload)
    check(r[2] == 0, "a text item is stored")
    a.usb(0x26)  # STOP: the name tag
    time.sleep(0.2)
    a.buttons(BTN_A)
    time.sleep(1.0)
    a.buttons(0)
    time.sleep(0.15)
    a.press(BTN_D)
    a.press(BTN_D)
    a.press(BTN_A)
    time.sleep(0.2)
    r = a.usb(QUERY)
    check(r[0] == MODE_FLASH_PATTERN and r[4] == 0, "the menu's third entry starts item 0 (mode %d, running %d)" % (r[0], r[4]))


if __name__ == "__main__":
    main()
