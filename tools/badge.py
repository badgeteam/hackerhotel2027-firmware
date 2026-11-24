#!/usr/bin/env python3
"""Talk to the badge's item store over USB, the same protocol as the web
interface (webusb_protocol.h). Needs pyusb (pip install pyusb).

  tools/badge.py list                      the items, which one runs, which one boots
  tools/badge.py add FILE [NAME]           store a .hhapp, .ledanim or .txt; it starts at once
                                           (.txt: --speed COLUMNS_PER_S, --plain)
  tools/badge.py run INDEX [--boot]        start an item (--boot: also at power-on)
  tools/badge.py stop                      stop the running animation or app
  tools/badge.py delete INDEX              remove one item; the items after it move down one
  tools/badge.py erase                     erase every item (the name and app data stay)
  tools/badge.py recommended               erase, then install the recommended app set
                                           (webui/apps/index.json; make -C apps webui)
  tools/badge.py name [TEXT]               show or set the owner's name
  tools/badge.py irlog [--seconds N]       print the infrared marks and spaces the badge
                                           receives, frame by frame (Ctrl-C to stop)
  tools/badge.py shoot TEAM [COMMAND [PARAMETER]]
                                           send a shot as a HH2025 banana or Fri3d 2022 Time
                                           Blaster does (command 1 shoot, 2 heal, 8 set team,
                                           10 a text byte; default 1, parameter 0)
"""
import argparse
import os
import re
import struct
import sys
import time
import unicodedata

import usb.core
import usb.util

VID, PID = 0x1209, 0xD643
EP_OUT, EP_IN = 0x02, 0x81
TYPES = {1: "animation", 2: "app", 3: "text"}
MODES = {1: "custom", 2: "animation or text", 3: "app", 4: "name tag"}
ERRORS = {1: "bad command or index", 2: "the store is full", 3: "no such item", 4: "not a valid animation, app or text"}
NONE = 0xFF


class Badge:
    def __init__(self):
        self.dev = usb.core.find(idVendor=VID, idProduct=PID)
        if self.dev is None:
            sys.exit("no badge found (USB %04x:%04x)" % (VID, PID))
        if self.dev.is_kernel_driver_active(0):
            self.dev.detach_kernel_driver(0)
        try:
            usb.util.claim_interface(self.dev, 0)
        except usb.core.USBError as e:
            if e.errno == 16:
                sys.exit("the badge is in use: close or disconnect the web interface first")
            raise

    def send(self, *data, timeout=5000):
        self.dev.write(EP_OUT, bytes(data), timeout=timeout)
        r = bytes(self.dev.read(EP_IN, 64, timeout=timeout))
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
        self.dev.write(EP_OUT, bytes([0x50]), timeout=1000)
        r = bytes(self.dev.read(EP_IN, 64, timeout=1000))
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


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
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
    n = sub.add_parser("name")
    n.add_argument("text", nargs="?")
    sh = sub.add_parser("shoot")
    sh.add_argument("team", type=int)
    sh.add_argument("command", type=int, nargs="?", default=1)
    sh.add_argument("parameter", type=int, nargs="?", default=0)
    sh.add_argument("--no-trigger", action="store_true", help="trigger bit 0 (default 1)")
    i = sub.add_parser("irlog")
    i.add_argument("--seconds", type=float, default=0, help="stop after this long (default: Ctrl-C)")
    args = p.parse_args()

    badge = Badge()
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
    elif args.cmd == "recommended":
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "webui", "apps")
        import json
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

    s = badge.send(0xFF)
    print("mode %s, %d items, %d bytes free, app API %d" % (MODES.get(s["mode"], s["mode"]), s["count"], s["free"], s["api"]))
    if args.cmd == "list":
        for i in range(s["count"]):
            item = badge.check(badge.send(0x24, i), "info")["item"]
            flags = ("running " if i == s["running"] else "") + ("boot" if i == s["boot"] else "")
            print("  %2d  %-9s %6d  %-20s %s" % (i, TYPES.get(item["type"], item["type"]), item["length"], item["name"], flags))


main()
