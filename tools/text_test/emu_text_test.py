#!/usr/bin/env python3
"""Text items on the emulated badge (tools/emu): stores the payloads that
text_item_test.js wrote (the web interface's makeTextItem), plays them, and
reads the text back off the centre grid with the glyphs from font.c.

  python3 tools/text_test/emu_text_test.py PAYLOAD_DIR

It runs the emulator headless (tools/emu/headless.py).
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


def main():
    payloads = sys.argv[1]
    items = {n[:-4]: open(os.path.join(payloads, n), "rb").read() for n in sorted(os.listdir(payloads)) if n.endswith(".hht")}
    texts = {n: open(os.path.join(payloads, n + ".txt")).read() for n in items}
    with tempfile.TemporaryDirectory() as tmp:
        emu = Emu(os.path.join(tmp, "flash.bin"))
        try:
            run(emu, items, texts)
        finally:
            emu.stop()
    print("%s  emulator: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


def run(emu, items, texts):
    emu.wait_frames()

    # A short text stands still, centred
    r = emu.store(TYPE_TEXT, "short", items["short"])
    check(r[2] == 0, "a text item is stored (result %d)" % r[2])
    index = r[4]
    check(r[0] == MODE_FLASH_PATTERN and r[4] == r[3] - 1, "it plays at once, as the last item")
    text = texts["short"]
    frame = emu.wait_frames()
    check(shows_still(frame, text), "%r shows centred on the grid" % text)
    check(any(frame[s] for s in range(384) if s not in GRID_SLOTS), "the name-tag style lights the arms and outline")
    time.sleep(0.3)
    check(shows_still(emu.wait_frames(), text), "a text that fits stands still")

    # The plain style: only the text
    r = emu.store(TYPE_TEXT, "plain", items["plain"])
    frame = emu.wait_frames()
    check(shows_still(frame, text), "plain: the text shows")
    check(not any(frame[s] for s in range(384) if s not in GRID_SLOTS), "plain: the arms and outline stay dark")

    # Accents were turned into ASCII by the page
    r = emu.store(TYPE_TEXT, "accents", items["accents"])
    frame = emu.wait_frames()
    check(scroll_offset(frame, texts["accents"]) is not None, "%r shows" % texts["accents"])

    # A long text scrolls to the left at its speed (60 columns per second)
    r = emu.store(TYPE_TEXT, "long", items["long"])
    long_index = r[4]
    text = texts["long"]
    o1 = scroll_offset(emu.wait_frames(), text)
    t1 = time.time()
    time.sleep(0.5)
    o2 = scroll_offset(emu.wait_frames(), text)
    t2 = time.time()
    check(o1 is not None and o2 is not None, "a long text shows a window of itself (%s, %s)" % (o1, o2))
    if o1 is not None and o2 is not None:
        moved = (o2 - o1) % (len(text_columns(text)) + 8)
        expect = (t2 - t1) * 60
        check(0.5 * expect < moved < 1.6 * expect, "it scrolls about 60 columns per second (%d columns in %.2f s)" % (moved, t2 - t1))

    # A text of 4000 words, about 22 KB, still draws at full speed
    r = emu.store(TYPE_TEXT, "huge", items["huge"])
    check(r[2] == 0, "a %d-byte text is stored" % len(items["huge"]))
    start, t0 = emu.frames, time.time()
    time.sleep(1)
    fps = (emu.frames - start) / (time.time() - t0)
    check(fps > 20, "it plays at %.0f frames per second" % fps)
    check(scroll_offset(emu.wait_frames(), texts["huge"]) is not None, "a window of the huge text shows")

    # Deleting an item before a playing text: the text moves down in flash
    # and plays on at its new index
    r = emu.usb(ITEM_RUN, long_index, 0)
    check(r[0] == MODE_FLASH_PATTERN and r[4] == long_index, "ITEM_RUN starts the long text again")
    r = emu.usb(ITEM_DELETE, index)
    check(r[2] == 0, "an item before it is deleted")
    for _ in range(50):
        r = emu.usb(QUERY)
        if r[4] == long_index - 1:
            break
        time.sleep(0.02)
    check(r[0] == MODE_FLASH_PATTERN and r[4] == long_index - 1, "the text plays on at index %d (mode %d, running %d)" % (long_index - 1, r[0], r[4]))
    check(scroll_offset(emu.wait_frames(), texts["long"]) is not None, "and still shows its text")

    # What the store refuses
    bad = bytearray(items["short"])
    bad[0:4] = b"HHT9"
    check(emu.store(TYPE_TEXT, "bad", bytes(bad))[2] == 4, "a wrong magic is refused (STORE_ERR_INVALID)")
    bad = bytearray(items["short"])
    bad[4] = 0
    check(emu.store(TYPE_TEXT, "bad", bytes(bad))[2] == 4, "speed 0 is refused")
    bad = bytearray(items["short"])
    bad[5] = 2
    check(emu.store(TYPE_TEXT, "bad", bytes(bad))[2] == 4, "an unknown style is refused")
    check(emu.store(TYPE_TEXT, "bad", items["short"][:8])[2] == 4, "a header without text is refused")
    check(emu.store(TYPE_ANIMATION, "bad", items["short"])[2] == 4, "a text is not taken as an animation")
    check(emu.store(4, "bad", items["short"])[2] == 1, "type 4 is refused (STORE_ERR_ARGS)")


if __name__ == "__main__":
    main()
