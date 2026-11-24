#!/usr/bin/env python3
"""Text items on the emulated badge (tools/emu): stores the payloads that
text_item_test.js wrote (the web interface's makeTextItem), plays them, and
reads the text back off the centre grid with the glyphs from font.c.

  python3 tools/text_test/emu_text_test.py EMU PAYLOAD_DIR

EMU is tools/emu/build/hh27-emu. It runs headless here: this script talks
to it on stdin/stdout as tools/emu/emu.py does ("u <hex>" = a USB packet,
"U <hex>" = the reply, "F <hex>" = a frame).
"""
import os
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
COLS = 24          # HH_COLS: slot = row * COLS + col
GRID_W, GRID_H = 17, 16
TEXT_Y = 4         # nametag.c

ITEM_BEGIN, ITEM_CHUNK, ITEM_COMMIT, ITEM_RUN, ITEM_DELETE, QUERY = 0x21, 0x22, 0x23, 0x25, 0x27, 0xFF
TYPE_ANIMATION, TYPE_TEXT = 1, 3
MODE_FLASH_PATTERN = 2


def load_grid():
    src = open(os.path.join(ROOT, "webui", "layout_data.js")).read()
    rows = re.search(r"const LAYOUT_GRID = (\[\[.*?\]\]);", src).group(1)
    return eval(rows.replace("null", "None"))  # a JSON array of arrays


def load_glyphs():
    """font.c's s_glyphs: {column tuple: character}"""
    src = open(os.path.join(ROOT, "font.c")).read()
    table = src[src.index("s_glyphs[95][FONT_W] = {"):]
    rows = re.findall(r"\{(0x[0-9A-Fa-f]{2}(?:, 0x[0-9A-Fa-f]{2}){4})\}", table)[:95]
    assert len(rows) == 95, "font.c: expected 95 glyphs"
    glyphs = {}
    for i, row in enumerate(rows):
        cols = tuple(int(v, 16) for v in row.split(", "))
        glyphs.setdefault(cols, chr(32 + i))  # the first wins: ' ' over other blanks
    return glyphs


GRID = load_grid()
GLYPHS = load_glyphs()
GRID_SLOTS = {s for row in GRID for s in row if s is not None}


class Emu:
    def __init__(self, exe, flash):
        self.p = subprocess.Popen([exe, flash, "0"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.replies = queue.Queue()
        self.frame = None
        self.frames = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            kind, _, rest = line.rstrip("\n").partition(" ")
            if kind == "U":
                self.replies.put(bytes.fromhex(rest))
            elif kind == "F":
                self.frame = bytes.fromhex(rest)
                self.frames += 1

    def usb(self, *packet):
        self.p.stdin.write("u " + bytes(packet).hex() + "\n")
        self.p.stdin.flush()
        return self.replies.get(timeout=10)

    def store(self, type_, name, payload):
        r = self.usb(ITEM_BEGIN, type_, *name.encode()[:20])
        if r[2]:
            return r
        for i in range(0, len(payload), 60):
            r = self.usb(ITEM_CHUNK, *payload[i:i + 60])
            if r[2]:
                return r
        return self.usb(ITEM_COMMIT)

    def wait_frames(self):
        """The LEDs a moment from now. The emulator sends a frame only when
        the LEDs change, so a text that stands still sends none."""
        end = time.time() + 5
        while self.frame is None:
            if time.time() > end:
                raise RuntimeError("the emulator sends no frames")
            time.sleep(0.005)
        time.sleep(0.1)
        return self.frame[1:]  # [0] is the global brightness

    def stop(self):
        self.p.kill()
        self.p.wait()


def grid_columns(frame):
    """The text rows of the grid as columns of bits (bit 0 = TEXT_Y), lit or not"""
    cols = []
    for x in range(GRID_W):
        v = 0
        for dy in range(8):
            slot = GRID[TEXT_Y + dy][x]
            if slot is not None and frame[slot]:
                v |= 1 << dy
        cols.append(v)
    return cols


# The text rows that have LEDs, per column: the octagon's corners have none
MASK = [sum(1 << dy for dy in range(8) if GRID[TEXT_Y + dy][x] is not None) for x in range(GRID_W)]
GLYPH_OF = {ch: cols for cols, ch in GLYPHS.items()}


def text_columns(text):
    """The text as font.c draws it: 5 glyph columns, then a blank one"""
    cols = []
    for c in text:
        cols += list(GLYPH_OF[c]) + [0]
    return cols[:-1]


def shows_still(frame, text):
    """True if the grid shows text, standing still and centred"""
    cols, want = grid_columns(frame), text_columns(text)
    x0 = (GRID_W - len(want)) // 2
    return all(cols[x] == (want[x - x0] if 0 <= x - x0 < len(want) else 0) & MASK[x] for x in range(GRID_W))


def scroll_offset(frame, text):
    """Where the scrolling text is: the text column at grid column 0 (with
    the start coming round 8 columns after the end); None if no offset fits"""
    cols, want = grid_columns(frame), text_columns(text) + [0] * 8  # nametag.c: GAP 8
    for o in range(len(want)):
        if all(cols[x] == want[(o + x) % len(want)] & MASK[x] for x in range(GRID_W)):
            return o
    return None


failures = 0


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def main():
    exe, payloads = sys.argv[1], sys.argv[2]
    items = {n[:-4]: open(os.path.join(payloads, n), "rb").read() for n in sorted(os.listdir(payloads)) if n.endswith(".hht")}
    texts = {n: open(os.path.join(payloads, n + ".txt")).read() for n in items}
    with tempfile.TemporaryDirectory() as tmp:
        emu = Emu(exe, os.path.join(tmp, "flash.bin"))
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
