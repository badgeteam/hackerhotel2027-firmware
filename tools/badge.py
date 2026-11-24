#!/usr/bin/env python3
"""Talk to the badge's item store over USB, the same protocol as the web
interface (webusb_protocol.h). Needs pyusb (pip install pyusb).

  tools/badge.py list                      the items, which one runs, which one boots
  tools/badge.py add FILE [NAME]           store a .hhapp or .ledanim; it starts at once
  tools/badge.py run INDEX [--boot]        start an item (--boot: also at power-on)
  tools/badge.py stop                      stop the running animation or app
  tools/badge.py delete INDEX              remove one item; the items after it move down one
  tools/badge.py erase                     erase every item (the name and app data stay)
  tools/badge.py name [TEXT]               show or set the owner's name
"""
import argparse
import os
import struct
import sys

import usb.core
import usb.util

VID, PID = 0x1209, 0xD643
EP_OUT, EP_IN = 0x02, 0x81
TYPES = {1: "animation", 2: "app"}
MODES = {1: "custom", 2: "animation", 3: "app", 4: "name tag"}
ERRORS = {1: "bad command or index", 2: "the store is full", 3: "no such item", 4: "not a valid animation or app"}
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

    def check(self, status, what):
        if status["result"]:
            sys.exit("%s: %s" % (what, ERRORS.get(status["result"], status["result"])))
        return status


def type_of(path, data):
    if data[:4] == b"HHAP":
        return 2
    if data[:4] == b"LA04":
        return 1
    sys.exit("%s: not a .hhapp or .ledanim" % path)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    a = sub.add_parser("add")
    a.add_argument("file")
    a.add_argument("name", nargs="?")
    r = sub.add_parser("run")
    r.add_argument("index", type=int)
    r.add_argument("--boot", action="store_true")
    sub.add_parser("stop")
    d = sub.add_parser("delete")
    d.add_argument("index", type=int)
    sub.add_parser("erase")
    n = sub.add_parser("name")
    n.add_argument("text", nargs="?")
    args = p.parse_args()

    badge = Badge()
    if args.cmd == "add":
        data = open(args.file, "rb").read()
        name = (args.name or os.path.splitext(os.path.basename(args.file))[0]).upper().encode()[:20]
        badge.check(badge.send(0x21, type_of(args.file, data), *name, timeout=10000), "begin")
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
