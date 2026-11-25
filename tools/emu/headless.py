"""The emulator (hh27-emu) without a window, for tests: start it, send it
USB packets and buttons, link two of them by infrared, and read the text
on the centre grid with the glyphs from font.c.

hh27-emu talks on stdin/stdout as tools/emu/emu.py does (hw.c lists the
lines): "u <hex>" a USB packet and "U <hex>" its reply, "b <hex>" the
buttons held, "F <hex>" a frame, "P <hex>" / "p <hex>" an IR packet sent /
received, "s <team> <trigger> <command> <parameter>" a banana shot received,
"v 1" / "v 0" USB power in or out, "c 0" / "c 1" / "c 2" the battery does
not charge / charges / is not there.

Emu(flash, fresh=True) starts as a newly flashed badge: it erases the
persistent record in that flash file, so the factory test runs at power-on.
"""
import os
import queue
import re
import subprocess
import threading
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
EXE = os.path.join(ROOT, "tools", "emu", "build", "hh27-emu")
COLS = 24          # HH_COLS: slot = row * COLS + col
GRID_W, GRID_H = 17, 16
TEXT_Y = 4         # nametag.c

ITEM_BEGIN, ITEM_CHUNK, ITEM_COMMIT, ITEM_RUN, ITEM_DELETE, NAME_SET, QUERY = 0x21, 0x22, 0x23, 0x25, 0x27, 0x31, 0xFF
BTN_A, BTN_C, BTN_B, BTN_D = 1, 2, 4, 8  # "b <hex>": bit 0 A, 1 C, 2 B, 3 D
SHOT_SHOOT, SHOT_HEAL, SHOT_TEAM, SHOT_BYTE = 1, 2, 8, 10  # a banana shot's command (ir_codec.h)
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
    def __init__(self, flash, exe=EXE, fresh=False, factory=False):
        """fresh: erase the persistent record first (the items stay), and run
        the factory test at power-on. factory: run the factory test at
        power-on if it has not passed yet, as the badge does (fresh does too)."""
        power_on = ["fresh"] if fresh else ["factory"] if factory else []
        self.p = subprocess.Popen([exe, flash, "0"] + power_on, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self.replies = queue.Queue()
        self.frame = None
        self.frames = 0
        self.ir_sent = []  # the IR packets it sent, as bytes (type, length, data)
        self.log = []      # the firmware's log lines ("L"), apps' print() too
        self.peers = []    # emulators that receive its IR packets (link())
        self.lock = threading.Lock()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            kind, _, rest = line.rstrip("\n").partition(" ")
            if kind == "U":
                self.replies.put(bytes.fromhex(rest))
            elif kind == "F":
                self.frame = bytes.fromhex(rest)
                self.frames += 1
            elif kind == "L":
                self.log.append(rest)
            elif kind == "P":
                self.ir_sent.append(bytes.fromhex(rest))
                for peer in self.peers:
                    peer.send("p " + rest)

    def send(self, line):
        with self.lock:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()

    def usb(self, *packet):
        self.send("u " + bytes(packet).hex())
        return self.replies.get(timeout=10)

    def buttons(self, held):
        self.send("b %x" % held)

    def press(self, button, ms=80):
        self.buttons(button)
        time.sleep(ms / 1000)
        self.buttons(0)
        time.sleep(0.08)

    def shot(self, team, command, parameter=0, trigger=1):
        """A banana shot comes in (with a good CRC)"""
        self.send("s %d %d %d %d" % (team, trigger, command, parameter))

    def usb_power(self, on):
        """USB power (VBUS, PC3) in or out. While it is out, the badge gets no
        USB packets, and the charger is off."""
        self.send("v %d" % bool(on))

    def charging(self, on):
        """The battery starts (on) or stops charging: CHRG (PB22) low or high,
        while USB power is in"""
        self.send("c %d" % bool(on))

    def no_battery(self):
        """No battery: CHRG toggles every 100 ms while USB power is in, as the
        charger does without a battery. charging() puts a battery back."""
        self.send("c 2")

    def set_name(self, name):
        return self.usb(NAME_SET, *name.encode())

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


def link(a, b):
    """IR packets from each one reach the other"""
    a.peers.append(b)
    b.peers.append(a)
