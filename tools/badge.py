#!/usr/bin/env python3
"""Talk to the badge's item store over USB, the same protocol as the web
interface (webusb_protocol.h). Needs pyusb (pip install pyusb).

  tools/badge.py list                      the badge's serial number, the items, which one
                                           runs, which one boots
  tools/badge.py add FILE [NAME]           store a .hhapp, .ledanim or .txt; it starts at once
                                           (.txt: --speed COLUMNS_PER_S, --plain)
  tools/badge.py run INDEX [--boot]        start an item (--boot: also at power-on)
  tools/badge.py stop                      stop the running animation or app
  tools/badge.py delete INDEX              remove one item; the items after it move down one
  tools/badge.py erase                     erase every item (the name and app data stay)
  tools/badge.py factory                   run the factory test now (docs/FACTORY_TEST.md):
                                           store it (apps/build/factory.hhapp) unless it is
                                           stored, and start it
  tools/badge.py recommended               erase, then install the recommended app set
                                           (webui/apps/index.json; make -C apps webui)
  tools/badge.py name [TEXT]               show or set the owner's name
  tools/badge.py brightness [N]            show or set the brightness, 1 .. 255; the badge
                                           keeps it (B / D step 8, 16, 32, 64, 128, 255)
  tools/badge.py irlog [--seconds N]       print the infrared marks and spaces the badge
                                           receives, frame by frame (Ctrl-C to stop)
  tools/badge.py shoot TEAM [COMMAND [PARAMETER]]
                                           send a shot as a HH2025 banana or Fri3d 2022 Time
                                           Blaster does (command 1 shoot, 2 heal, 8 set team,
                                           10 a text byte; default 1, parameter 0)
  tools/badge.py bringup [--from WHERE] [--kind grid|edge|rgb] [--lines rows|cols]
                         [--auto SECONDS] [--count N] [--level 1..255]
                                           light the LEDs one at a time, and print each one's
                                           designator, kind, COM line and PWM channel, for a
                                           new board (docs/BRINGUP.md). Enter: next, b: back,
                                           x: mark it bad, q: quit. --lines lights a whole COM
                                           line or PWM channel at a time, to find shorts.
                                           At the end the badge shows its name tag again.

With more than one badge plugged in, pick one with --serial SERIAL before
the command (list shows each badge's serial number). Badges with older
firmware all have serial 001: of those, the first one found is used.
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import time
import unicodedata

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
VID, PID = 0x1209, 0xD643
EP_OUT, EP_IN = 0x02, 0x81
TYPES = {1: "animation", 2: "app", 3: "text"}
MODES = {1: "custom", 2: "animation or text", 3: "app", 4: "name tag"}
ERRORS = {1: "bad command or index", 2: "the store is full", 3: "no such item", 4: "not a valid animation, app or text"}
NONE = 0xFF
FACTORY_API = 9  # the app API the factory test app needs (apps/hhapp.h)
FACTORY_APP = os.path.join(ROOT, "apps", "build", "factory.hhapp")


def serial_of(dev):
    """The serial number: 16 hex digits from the chip's unique ID (older
    firmware: 001)"""
    import usb.core
    import usb.util
    try:
        # older firmware also sends the string's terminator
        return (usb.util.get_string(dev, dev.iSerialNumber) or "").rstrip("\0") or "?"
    except (usb.core.USBError, ValueError):
        return "?"  # no permission to read it


class Badge:
    def __init__(self, serial=None):
        import usb.core  # here, so the tests can import this file without pyusb
        import usb.util
        devs = list(usb.core.find(find_all=True, idVendor=VID, idProduct=PID))
        if serial:
            devs = [d for d in devs if serial_of(d).upper() == serial.upper()]
        if not devs:
            sys.exit("no badge %sfound (USB %04x:%04x)" % ("with serial %s " % serial if serial else "", VID, PID))
        serials = [serial_of(d) for d in devs]
        if len(set(serials)) > 1:
            sys.exit("%d badges found; pick one with --serial: %s" % (len(devs), ", ".join(serials)))
        if len(devs) > 1:
            # older firmware gives every badge the serial 001, so --serial
            # cannot tell them apart: the first one, as before
            print("warning: %d badges with serial %s (older firmware); using the first. A firmware update "
                  "gives each badge its own serial." % (len(devs), serials[0]), file=sys.stderr)
        self.dev = devs[0]
        self.serial = serial_of(self.dev)
        if self.dev.is_kernel_driver_active(0):
            self.dev.detach_kernel_driver(0)
        try:
            usb.util.claim_interface(self.dev, 0)
        except usb.core.USBError as e:
            if e.errno == 16:
                sys.exit("the badge is in use: close or disconnect the web interface first")
            raise

    def transfer(self, data, timeout):
        """One command out, its reply in"""
        self.dev.write(EP_OUT, data, timeout=timeout)
        return bytes(self.dev.read(EP_IN, 64, timeout=timeout))

    def send(self, *data, timeout=5000):
        r = self.transfer(bytes(data), timeout)
        status = dict(mode=r[0], brightness=r[1], result=r[2], count=r[3], running=r[4], boot=r[5],
                      free=r[6] * 256, api=r[7])
        if len(r) == 40:
            status["name"] = r[8:40].rstrip(b"\0").decode(errors="replace")
        elif len(r) >= 33 and r[2] == 0:
            status["item"] = dict(type=r[8], length=struct.unpack("<I", r[9:13])[0],
                                  name=r[13:33].rstrip(b"\0").decode(errors="replace"))
        return status

    def ir_raw(self):
        """The marks and spaces received since the last call: (mark, us) pairs."""
        r = self.transfer(bytes([0x50]), 1000)
        n = r[8] if len(r) > 8 else 0
        out = []
        for i in range(n):
            v = r[9 + 2 * i] | r[10 + 2 * i] << 8
            out.append((bool(v & 0x8000), v & 0x7FFF))
        return out

    def check(self, status, what):
        if status["result"]:
            sys.exit("%s: %s" % (what, ERRORS.get(status["result"], status["result"])))
        return status


def type_of(path, data):
    if data[:4] == b"HHAP":
        return 2
    if data[:4] == b"LA04":
        return 1
    sys.exit("%s: not a .hhapp, .ledanim or .txt" % path)


def text_item(text, speed, plain):
    """A text item (nametag.h), as webui/text_item.js makes it, in short:
    accents dropped, a line break as two spaces, other non-ASCII as '?'"""
    out = []
    for ch in unicodedata.normalize("NFKD", text):
        if unicodedata.combining(ch) or ch == "\r":
            continue
        out.append("  " if ch == "\n" else " " if ch == "\t" else ch if " " <= ch <= "~" else "?")
    ascii = re.sub(" {2,}", "  ", "".join(out)).strip()
    if not ascii:
        sys.exit("the text is empty")
    if not 1 <= speed <= 60:
        sys.exit("--speed: 1 to 60 columns per second")
    return b"HHT1" + bytes([speed, 1 if plain else 0, 0, 0]) + ascii.encode("ascii")


def ir_frame(segments):
    """One frame's marks and spaces, as text, and its bits read as NEC-like
    pulse distance (a space over 1 ms is a 1), LSB first."""
    line = " ".join("%s%d" % ("M" if m else "S", us) for m, us in segments)
    # the bits: the spaces after the leader (mark, space), each after a mark
    spaces = [us for i, (m, us) in enumerate(segments) if not m and i >= 2]
    bits = [1 if us > 1000 else 0 for us in spaces]
    value = sum(b << i for i, b in enumerate(bits))
    marks = [us for m, us in segments if m][1:]
    desc = "%d bits" % len(bits)
    if bits:
        desc += ", 0x%0*X LSB first" % ((len(bits) + 3) // 4, value)
        desc += ", bytes " + " ".join("%02X" % (value >> (8 * k) & 0xFF) for k in range((len(bits) + 7) // 8))
    if marks:
        desc += "; marks %d .. %d us" % (min(marks), max(marks))
    return line, desc


def irlog(badge, seconds):
    """Prints what the receiver gets, a frame at a time: a space of 20 ms or
    more ends a frame."""
    frame, quiet_since = [], time.time()
    end = time.time() + seconds if seconds else None
    print("listening; point a remote, banana or blaster at the badge (Ctrl-C to stop)")
    try:
        while end is None or time.time() < end:
            got = badge.ir_raw()
            for m, us in got:
                if not m and us >= 20000:
                    if frame:
                        line, desc = ir_frame(frame)
                        print("leader M%d S%d: %s" % (frame[0][1], frame[1][1] if len(frame) > 1 else 0, desc))
                        print("   " + line)
                    frame = []
                    continue
                if not frame and not m:
                    continue  # a frame starts with a mark
                frame.append((m, us))
            if got:
                quiet_since = time.time()
            elif frame and time.time() - quiet_since > 0.1:
                # the last space of a frame comes only with the next mark:
                # after 100 ms of nothing, the frame is over
                line, desc = ir_frame(frame)
                print("leader M%d S%d: %s" % (frame[0][1], frame[1][1] if len(frame) > 1 else 0, desc))
                print("   " + line)
                frame = []
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass


# --- Bring-up: one LED at a time (docs/BRINGUP.md) ---------------------------

COLS = 24  # PWM channels PA0..PA23; the 16 rows are the COM lines PB0..PB15
FRAME_CHUNK, FRAME_COMMIT, STOP = 0x02, 0x03, 0x26
ARMS = ["top arm", "upper right arm", "lower right arm", "lower left arm", "upper left arm"]


def load_js(name, var=None):
    """A JSON value from a file in webui/: the object in board_data.js, or
    the array after `const VAR =` in layout_data.js"""
    s = open(os.path.join(ROOT, "webui", name)).read()
    if var is None:
        return json.loads(s[s.index("{"):s.rindex("}") + 1])
    return json.loads(re.search(r"const %s = (\[.*?\]);\n" % var, s).group(1))


class Step:
    """What one step of the walk lights: slots, and the text that names them"""
    def __init__(self, slots, refs, text):
        self.slots, self.refs, self.text = slots, refs, text


LED_LINE = "%-5s %-6s %-4s  %-4s %-4s %-4s %4s  %s"  # one LED: its columns


def led_line(slot, ref, colour, kind, where):
    # Hardware f6015de: PWM channel PA<n> goes through column resistor
    # R<n+1>, COM line PB<n> is net C<n+1> (docs/BRINGUP.md, "The LED matrix")
    row, col = divmod(slot, COLS)
    return LED_LINE % (ref, colour, kind, "PB%d" % row, "PA%d" % col, "R%d" % (col + 1), slot, where)


def bringup_leds():
    """Every LED channel, in the order of LA04 (webui/layout_data.js): the
    centre grid row by row, the outline clockwise from the top, then the RGB
    LEDs arm by arm from the centre out, red, green and blue in turn.
    Returns {slot: (designator, colour or "", kind, where)} and the order."""
    board = load_js("board_data.js")
    order = load_js("layout_data.js", "LAYOUT_ORDER")
    grid = load_js("layout_data.js", "LAYOUT_GRID")
    at = {}
    for y, row in enumerate(grid):
        for x, slot in enumerate(row):
            if slot is not None:
                at[slot] = "grid row %d, column %d" % (y + 1, x + 1)
    leds = {}
    for l in board["leds"]:
        slot = l["row"] * COLS + l["col"]
        leds[slot] = (l["ref"], "", "grid" if slot in at else "edge", at.get(slot, ""))
    edge = [s for s in order if leds.get(s, ("", "", ""))[2] == "edge"]
    for n, slot in enumerate(edge):
        leds[slot] = leds[slot][:3] + ("outline %d of %d, clockwise from the top" % (n + 1, len(edge)),)

    # The arm an RGB LED is on, by its angle from the grid's centre, and
    # its place on the arm, counted from the centre, as tools/gen_layout.py
    # works them out for layout.c (tools/bringup_test checks that they agree)
    gx = [l["x"] for l in board["leds"] if leds[l["row"] * COLS + l["col"]][2] == "grid"]
    gy = [l["y"] for l in board["leds"] if leds[l["row"] * COLS + l["col"]][2] == "grid"]
    cx, cy = (min(gx) + max(gx)) / 2, (min(gy) + max(gy)) / 2
    arms = {}
    for l in board["rgb_leds"]:
        dx, dy = l["x"] - cx, l["y"] - cy
        arm = round(math.degrees(math.atan2(dx, -dy)) % 360 / 72) % 5  # clockwise from the top
        arms.setdefault(arm, []).append((math.hypot(dx, dy), l))
    for arm, on_arm in arms.items():
        for rank, (_, l) in enumerate(sorted(on_arm, key=lambda t: t[0])):
            where = "%s, %d from the centre" % (ARMS[arm], rank + 1)
            for colour, key in (("red", "row"), ("green", "green_row"), ("blue", "blue_row")):
                leds[l[key] * COLS + l["col"]] = (l["ref"], colour, "rgb", where)
    assert sorted(leds) == sorted(order) == list(range(16 * COLS)), "board_data.js: not one LED per slot"
    return leds, order


def bringup_steps(kind=None, lines=None):
    """The steps of the walk: one LED channel each, or with lines="rows" /
    "cols" one COM line / PWM channel each. kind: only "grid", "edge" or "rgb"."""
    leds, order = bringup_leds()
    keep = [s for s in order if kind in (None, leds[s][2])]
    if not lines:
        steps = []
        for slot in keep:
            ref, colour, k, where = leds[slot]
            steps.append(Step([slot], [ref], led_line(slot, ref, colour, k, where)))
    else:
        steps = []
        count = 16 if lines == "rows" else COLS
        for n in range(count):
            slots = sorted(s for s in keep if (s // COLS if lines == "rows" else s % COLS) == n)
            if not slots:
                continue
            name = "COM PB%d (net C%d)" % (n, n + 1) if lines == "rows" else "PWM PA%d (R%d)" % (n, n + 1)
            refs = [leds[s][0] for s in slots]
            parts = " ".join(leds[s][0] + ("/" + leds[s][1][0].upper() if leds[s][1] else "") for s in slots)
            steps.append(Step(slots, refs, "%s, %d LEDs: %s" % (name, len(slots), parts)))
    for i, st in enumerate(steps):
        st.text = "%3d/%d  %s" % (i + 1, len(steps), st.text)
    return steps


def bringup_header(steps, lines=None):
    """The line above the walk that names the columns"""
    if lines:
        return ""
    width = len("%3d/%d" % (len(steps), len(steps)))  # as bringup_steps numbers them
    return "%*s  " % (width, "step") + LED_LINE % ("part", "colour", "kind", "COM", "PWM", "res", "slot", "place")


def find_step(steps, where):
    """--from: a step number as printed (1 = the first), or a designator"""
    if where.isdigit():
        if not 1 <= int(where) <= len(steps):
            sys.exit("--from: 1 to %d" % len(steps))
        return int(where) - 1
    for i, st in enumerate(steps):
        if where.upper() in st.refs:
            return i
    sys.exit("--from: no %s in this walk" % where)


def show(badge, slots, level):
    """The frame with only these slots lit, as the web interface's live
    preview sends it (webui/webusb.js, uploadFrame)"""
    frame = bytearray(16 * COLS)
    for s in slots:
        frame[s] = level
    for off in range(0, len(frame), 58):
        badge.send(FRAME_CHUNK, off & 0xFF, off >> 8, *frame[off:off + 58])
    badge.send(FRAME_COMMIT)


def bringup(badge, steps, start=0, auto=None, count=None, level=255, ask=input, out=print, header=""):
    """Lights the steps one by one. auto: seconds per step, else ask() for a
    key after each one (Enter: next, b: back, x: mark bad, q: quit). Ends
    with STOP, as Stop playing in the web interface: the badge shows its
    name tag again. Returns the steps marked bad."""
    bad = []
    s = badge.send(0xFF)
    if s["brightness"] < 255:
        out("the badge's brightness is %d of 255: B on the badge makes it brighter" % s["brightness"])
    if not auto:
        out("Enter: next, b: back, x: mark as bad and go on, q: quit")
    if header:
        out(header)
    i, shown = start, 0
    try:
        while i < len(steps) and (count is None or shown < count):
            st = steps[i]
            show(badge, st.slots, level)
            shown += 1
            if auto:
                out(st.text)
                time.sleep(auto)
                i += 1
                continue
            key = ask(st.text + "  ").strip().lower()
            if key == "q":
                break
            if key == "b":
                i = max(0, i - 1)
                continue
            if key == "x":
                bad.append(st)
            i += 1
    except (KeyboardInterrupt, EOFError):
        out("")
    except Exception as e:  # unplugged, or a USB error: still list the bad ones
        out("lost the badge: %s" % e)
    finally:
        try:
            badge.send(STOP)
        except Exception as e:  # unplugged: nothing to give back
            out("could not give the badge back: %s" % e)
    if bad:
        out("marked bad:")
        for st in bad:
            out("  " + st.text)
    return bad


def factory(badge):
    """The factory test is an app (apps/factory), stored as FACTORY: the
    firmware starts it at power-on while it is stored, and deletes it once
    it has passed. Stores it if it is not stored, and starts it."""
    s = badge.send(0xFF)
    if s["api"] < FACTORY_API:
        sys.exit("the badge's firmware has app API %d; the factory test needs %d: update the firmware" % (s["api"], FACTORY_API))
    for i in range(s["count"]):
        item = badge.check(badge.send(0x24, i), "info")["item"]
        if item["type"] == 2 and item["name"] == "FACTORY":
            badge.check(badge.send(0x25, i), "run")
            print("factory test started on the badge (item %d)" % i)
            return
    if not os.path.exists(FACTORY_APP):
        sys.exit("%s is missing: run make -C apps first" % FACTORY_APP)
    data = open(FACTORY_APP, "rb").read()
    badge.check(badge.send(0x21, 2, *b"FACTORY", timeout=10000), "begin")
    for off in range(0, len(data), 60):
        badge.check(badge.send(0x22, *data[off:off + 60]), "write")
    done = badge.check(badge.send(0x23), "commit")  # it starts, and becomes the boot item
    index = done["count"] - 1
    if s["boot"] != NONE:  # the boot item from before, back: run it, then the test again
        badge.check(badge.send(0x25, s["boot"], 1), "boot item")
        badge.check(badge.send(0x25, index), "run")
    print("factory test stored as item %d and started on the badge; it deletes itself once it passed" % index)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--serial", help="the badge with this serial number, when more than one is plugged in")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    a = sub.add_parser("add")
    a.add_argument("file")
    a.add_argument("name", nargs="?")
    a.add_argument("--speed", type=int, default=10, help="a .txt: columns per second when it scrolls (1..60)")
    a.add_argument("--plain", action="store_true", help="a .txt: only the text, no colours on the arms")
    r = sub.add_parser("run")
    r.add_argument("index", type=int)
    r.add_argument("--boot", action="store_true")
    sub.add_parser("stop")
    d = sub.add_parser("delete")
    d.add_argument("index", type=int)
    sub.add_parser("erase")
    sub.add_parser("recommended")
    sub.add_parser("factory")
    n = sub.add_parser("name")
    n.add_argument("text", nargs="?")
    b = sub.add_parser("brightness")
    b.add_argument("value", type=int, nargs="?")
    sh = sub.add_parser("shoot")
    sh.add_argument("team", type=int)
    sh.add_argument("command", type=int, nargs="?", default=1)
    sh.add_argument("parameter", type=int, nargs="?", default=0)
    sh.add_argument("--no-trigger", action="store_true", help="trigger bit 0 (default 1)")
    i = sub.add_parser("irlog")
    i.add_argument("--seconds", type=float, default=0, help="stop after this long (default: Ctrl-C)")
    b = sub.add_parser("bringup")
    b.add_argument("--from", dest="start", metavar="WHERE", help="start here: a step number or a designator (D35)")
    b.add_argument("--kind", choices=["grid", "edge", "rgb"], help="only the centre grid, the outline or the RGB LEDs")
    b.add_argument("--lines", choices=["rows", "cols"], help="a whole COM line (rows) or PWM channel (cols) at a time")
    b.add_argument("--auto", type=float, metavar="SECONDS", help="go on by itself after this long (default: wait for Enter)")
    b.add_argument("--count", type=int, help="stop after this many steps")
    b.add_argument("--level", type=int, default=255, help="the LED value, 1..255 (default 255)")
    args = p.parse_args()

    if args.cmd == "bringup":
        if not 1 <= args.level <= 255:
            sys.exit("--level: 1 to 255")
        if (args.auto is not None and args.auto <= 0) or (args.count is not None and args.count < 1):
            sys.exit("--auto and --count: more than 0")
        steps = bringup_steps(args.kind, args.lines)
        start = find_step(steps, args.start) if args.start else 0
        bringup(Badge(args.serial), steps, start, args.auto, args.count, args.level,
                header=bringup_header(steps, args.lines))
        return
    badge = Badge(args.serial)
    if args.cmd == "irlog":
        badge.ir_raw()  # drop what came in before
        irlog(badge, args.seconds)
        return
    if args.cmd == "shoot":
        if not (0 <= args.team < 8 and 0 <= args.command < 16 and 0 <= args.parameter < 16):
            sys.exit("team 0..7, command 0..15, parameter 0..15")
        badge.check(badge.send(0x51, args.team, 0 if args.no_trigger else 1, args.command, args.parameter), "shoot")
        print("shot: team %d, command %d, parameter %d" % (args.team, args.command, args.parameter))
        return
    if args.cmd == "add":
        data = open(args.file, "rb").read()
        name = (args.name or os.path.splitext(os.path.basename(args.file))[0]).upper().encode()[:20]
        if args.file.lower().endswith(".txt"):
            data, kind = text_item(data.decode("utf-8", "replace"), args.speed, args.plain), 3
        else:
            kind = type_of(args.file, data)
        badge.check(badge.send(0x21, kind, *name, timeout=10000), "begin")
        for off in range(0, len(data), 60):
            badge.check(badge.send(0x22, *data[off:off + 60]), "write")
        s = badge.check(badge.send(0x23), "commit")
        print("stored %s as item %d (%d bytes), %d bytes free" % (name.decode(), s["count"] - 1, len(data), s["free"]))
    elif args.cmd == "run":
        badge.check(badge.send(0x25, args.index, 1 if args.boot else 0), "run")
    elif args.cmd == "stop":
        badge.send(0x26)
    elif args.cmd == "delete":
        badge.check(badge.send(0x27, args.index, timeout=10000), "delete")
    elif args.cmd == "erase":
        badge.send(0x20, timeout=10000)
    elif args.cmd == "factory":
        factory(badge)
    elif args.cmd == "recommended":
        here = os.path.join(ROOT, "webui", "apps")
        index = json.load(open(os.path.join(here, "index.json")))
        files = {a["name"]: a["file"] for a in index["apps"]}
        names = index.get("recommended", [])
        if not names:
            sys.exit("index.json has no recommended set (make -C apps webui)")
        blobs = [(n, open(os.path.join(here, files[n]), "rb").read()) for n in names]  # all read first
        badge.send(0x20, timeout=10000)
        for k, (n, data) in enumerate(blobs):
            badge.check(badge.send(0x21, 2, *n.encode()[:20], timeout=10000), "begin")
            for off in range(0, len(data), 60):
                badge.check(badge.send(0x22, *data[off:off + 60]), "write")
            s = badge.check(badge.send(0x23), "commit")
            print("%2d/%d  %-10s %5d bytes, %d bytes free" % (k + 1, len(blobs), n, len(data), s["free"]))
    elif args.cmd == "name":
        s = badge.send(0x31, *args.text.encode()[:32]) if args.text is not None else badge.send(0x30)
        print("name: %r" % s["name"])
    elif args.cmd == "brightness":
        if args.value is not None and not 1 <= args.value <= 255:
            sys.exit("brightness: 1 to 255")
        s = badge.check(badge.send(0x32, args.value), "brightness") if args.value is not None else badge.send(0xFF)
        if args.value is not None and s["brightness"] != args.value:
            sys.exit("the badge's firmware is too old to set the brightness: update it")
        print("brightness: %d of 255" % s["brightness"])

    if args.cmd == "list":
        print("serial %s" % badge.serial)
    s = badge.send(0xFF)
    print("mode %s, %d items, %d bytes free, app API %d" % (MODES.get(s["mode"], s["mode"]), s["count"], s["free"], s["api"]))
    if args.cmd == "list":
        for i in range(s["count"]):
            item = badge.check(badge.send(0x24, i), "info")["item"]
            flags = ("running " if i == s["running"] else "") + ("boot" if i == s["boot"] else "")
            print("  %2d  %-9s %6d  %-20s %s" % (i, TYPES.get(item["type"], item["type"]), item["length"], item["name"], flags))


if __name__ == "__main__":
    main()
