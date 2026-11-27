#!/usr/bin/env python3
"""Raw infrared (ir_send_raw, HH_API_VERSION 7) on an emulated badge
(tools/emu/headless.py), with the test app tools/ir_test/raw_app.c: what
the calls return, the frames on air, and that stopping the app stops its
frame, so the next app can send at once.

  python3 tools/ir_test/emu_raw_test.py RAW_APP.hhapp
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

TYPE_APP = 2
STOP = 0x26
SONY = [2400, 600, 1200, 600, 600, 600, 1200, 600, 600, 25000]  # raw_app.c
LONG = [500, 50000] * 20

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
        time.sleep(0.02)
    return False


def reports(emu):
    """The app's "RAWTEST <what> <0 or 1>" lines, in order: [(what, ok)]"""
    out = []
    for line in list(emu.log):
        words = line.split()
        if len(words) == 3 and words[0] == "RAWTEST":
            out.append((words[1], words[2] == "1"))
    return out


def main():
    app = open(sys.argv[1], "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        emu = Emu(os.path.join(tmp, "flash.bin"))
        try:
            run(emu, app)
        finally:
            emu.stop()
    print("%s  raw infrared: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


def run(emu, app):
    check(emu.store(TYPE_APP, "rawtest", app)[2] == 0, "the test app is stored and runs")
    check(wait_for(lambda: len(reports(emu)) >= 5, 5), "it reports all five calls")
    want = [("first", True), ("busy", False), ("carrier", False), ("count", False), ("long", True)]
    for (what, ok), got in zip(want, reports(emu)):
        check(got == (what, ok), "%s: %s (got %s)" % (what, "sent" if ok else "refused", got))
    check(emu.ir_raw[:2] == [(40000, SONY), (38000, LONG)],
          "on air: the Sony frame at 40 kHz, then the long one at 38 kHz (%d frames)" % len(emu.ir_raw))

    # The long frame takes 2 s. Stop the app halfway, start it again: its
    # first frame goes out at once, because the old one stopped with the app.
    time.sleep(0.3)
    emu.usb(STOP)
    emu.usb(ITEM_RUN, 0, 0)
    check(wait_for(lambda: len(reports(emu)) >= 6, 5), "the app starts again")
    check(reports(emu)[5:6] == [("first", True)], "its first frame goes out at once: the old one stopped with the app")
    check(len(emu.ir_raw) == 3 and emu.ir_raw[2] == (40000, SONY), "on air: the Sony frame again")


if __name__ == "__main__":
    main()
