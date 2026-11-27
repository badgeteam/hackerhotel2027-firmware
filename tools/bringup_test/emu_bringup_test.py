#!/usr/bin/env python3
"""tools/badge.py bringup on the emulator (tools/emu/headless.py), without
pyusb: for each line the walk prints, the LEDs of the designator on that
line, looked up in webui/board_data.js here, are the only ones lit. Then
the badge shows its name tag again.

  python3 tools/bringup_test/emu_bringup_test.py
"""
import json
import os
import re
import sys
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "emu"))
sys.path.insert(0, os.path.join(here, ".."))
from headless import Emu, ROOT  # noqa: E402
import badge  # noqa: E402

MODE_NAMETAG = 4
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


# Each designator's slots, from the board data, not from badge.py
s = open(os.path.join(ROOT, "webui", "board_data.js")).read()
board = json.loads(s[s.index("{"):s.rindex("}") + 1])
SLOTS = {l["ref"]: {"": l["row"] * 24 + l["col"]} for l in board["leds"]}
for l in board["rgb_leds"]:
    SLOTS[l["ref"]] = {c: l[k] * 24 + l["col"] for c, k in (("red", "row"), ("green", "green_row"), ("blue", "blue_row"))}


class EmuBadge(badge.Badge):
    """badge.py's Badge, talking to the emulator instead of pyusb"""
    def __init__(self, emu):
        self.emu = emu

    def transfer(self, data, timeout):
        return self.emu.usb(*data)


def lit(e):
    return {i for i, v in enumerate(e.frame[1:]) if v} if e.frame else set()


def parse(line):
    """A printed LED line, 'n/N  REF [colour] kind ...': (ref, colour, kind)"""
    words = line.split()
    if words[2] in ("red", "green", "blue"):
        return words[1], words[2], words[3]
    return words[1], "", words[2]


class Watch:
    """out() and ask() for badge.bringup(): checks the LEDs at each step"""
    def __init__(self, e, keys=()):
        self.e, self.keys, self.seen, self.summary = e, list(keys), [], False

    def step(self, line):
        self.summary = self.summary or line.startswith("marked bad")
        if self.summary or not line.split() or "/" not in line.split()[0]:
            return  # a header, a note or the summary, not a step
        self.seen.append(line)
        if "LEDs:" in line:  # --lines: every designator on the line
            want = set()
            for part in line.split(": ", 1)[1].split():
                ref, _, c = part.partition("/")
                want.add(SLOTS[ref][{"R": "red", "G": "green", "B": "blue", "": ""}[c]])
        else:
            ref, colour, _ = parse(line)
            want = {SLOTS[ref][colour]}
        ok = wait_for(lambda: lit(self.e) == want, 2)
        check(ok, "%s: lit %s, want %s" % (line.strip()[:60], sorted(lit(self.e))[:6], sorted(want)[:6]))

    def out(self, line):
        self.step(line)

    def ask(self, prompt):
        self.step(prompt)
        return self.keys.pop(0) if self.keys else "q"


def given_back(e, b):
    return b.send(0xFF)["mode"] == MODE_NAMETAG


def run(e):
    e.wait_frames()
    b = EmuBadge(e)

    # Auto mode: the first grid LEDs, then across from the grid to the outline
    steps = badge.bringup_steps()
    w = Watch(e)
    badge.bringup(b, steps, 0, auto=0.05, count=3, out=w.out)
    check(len(w.seen) == 3, "auto, count 3: three steps (%d)" % len(w.seen))
    check(given_back(e, b), "then the name tag again")
    w = Watch(e)
    badge.bringup(b, steps, 206, auto=0.05, count=4, out=w.out)
    check([parse(l)[2] for l in w.seen] == ["grid", "grid", "edge", "edge"], "grid, then the outline")

    # From a designator: an RGB LED gives red, green, blue, then the next one
    rgb = badge.bringup_steps("rgb")
    start = badge.find_step(rgb, "d155")
    w = Watch(e)
    badge.bringup(b, rgb, start, auto=0.05, count=4, out=w.out)
    check([parse(l)[:2] for l in w.seen][:3] == [("D155", "red"), ("D155", "green"), ("D155", "blue")],
          "--from D155 --kind rgb: red, green, blue: %s" % [parse(l)[:2] for l in w.seen])
    check(w.seen[3].split()[1] != "D155", "then the next RGB LED")

    # Waiting for keys: Enter, b back, x marks it, q quits and gives the badge back
    w = Watch(e, ["", "b", "", "x", "q"])
    bad = badge.bringup(b, steps, 10, out=w.out, ask=w.ask)
    nums = [l.split()[0] for l in w.seen]
    check(nums == ["11/384", "12/384", "11/384", "12/384", "13/384"], "Enter, b, Enter, x, q: %s" % nums)
    check([st.text.split()[0] for st in bad] == ["12/384"], "x marks that LED as bad")
    check(given_back(e, b), "q gives the badge back")

    # Ctrl-C gives it back too
    def interrupt(prompt):
        raise KeyboardInterrupt
    badge.bringup(b, steps, 0, out=lambda line: None, ask=interrupt)
    check(given_back(e, b), "Ctrl-C gives the badge back")

    # Whole lines: a COM line, then a PWM channel
    w = Watch(e)
    badge.bringup(b, badge.bringup_steps(lines="rows"), 0, auto=0.05, count=2, out=w.out)
    check(len(w.seen) == 2 and "COM PB1 " in w.seen[1], "--lines rows: PB0, PB1")
    w = Watch(e)
    badge.bringup(b, badge.bringup_steps(lines="cols"), 17, auto=0.05, count=1, out=w.out)
    check(len(w.seen) == 1 and "PWM PA17 (R18), 16 LEDs" in w.seen[0], "--lines cols: PA17, 16 LEDs")

    # The walk itself: every LED channel once
    steps = badge.bringup_steps()
    slots = [st.slots[0] for st in steps]
    check(sorted(slots) == list(range(384)), "384 steps, every slot once")
    check([len(badge.bringup_steps(k)) for k in ("grid", "edge", "rgb")] == [208, 71, 105], "208 grid, 71 outline, 105 RGB")

    # The header's columns line up with the steps, also with fewer than 100
    for kind in (None, "grid", "edge", "rgb"):
        st = badge.bringup_steps(kind)
        head = badge.bringup_header(st)
        check(head.index("part") == st[0].text.index(st[0].refs[0]), "header lines up (%s)" % (kind or "all"))

    # The arm and the place on it agree with layout.c (gen_layout.py)
    leds, _ = badge.bringup_leds()
    rgb = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}, // (D\d+)",
                     open(os.path.join(ROOT, "layout.c")).read())
    wrong = [ref for r, g, bl, arm, rank, ref in rgb
             if leds[int(r)] != (ref, "red", "rgb", "%s, %d from the centre" % (badge.ARMS[int(arm)], int(rank) + 1))
             or leds[int(g)][:2] != (ref, "green") or leds[int(bl)][:2] != (ref, "blue")]
    check(len(rgb) == 35 and not wrong, "35 RGB LEDs: arm and place as in layout.c %s" % wrong)

    # A USB error half way: a note, and still the bad list
    class Lost(EmuBadge):
        calls = 0

        def transfer(self, data, timeout):
            Lost.calls += 1
            if Lost.calls > 20:
                raise OSError("no such device")
            return super().transfer(data, timeout)
    lines = []
    keys = ["x"]
    bad = badge.bringup(Lost(e), steps, 0, out=lines.append, ask=lambda p: keys.pop(0) if keys else "")
    check(len(bad) == 1 and any(l.startswith("lost the badge") for l in lines) and "marked bad:" in lines,
          "a USB error: a note, then the bad list")
    check(any(l.startswith("could not give the badge back") for l in lines), "and a note that STOP failed")
    b.send(badge.STOP)


def command_line(e):
    """tools/badge.py bringup from the command line: parsed and run, not only
    the function (a lost dispatch once made it print the status and stop)"""
    import contextlib
    import io
    saved_badge, saved_argv = badge.Badge, sys.argv
    badge.Badge = lambda serial=None: EmuBadge(e)
    sys.argv = ["badge.py", "bringup", "--kind", "edge", "--auto", "0.05", "--count", "2"]
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            badge.main()
    finally:
        badge.Badge, sys.argv = saved_badge, saved_argv
    lines = out.getvalue().splitlines()
    check(any(l.strip().startswith("1/71") for l in lines) and any(l.strip().startswith("2/71") for l in lines),
          "badge.py bringup from the command line walks the LEDs (%d lines)" % len(lines))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        e = Emu(os.path.join(tmp, "a.bin"))
        try:
            run(e)
            command_line(e)
        finally:
            e.stop()
    print("%s  bring-up walk: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


main()
