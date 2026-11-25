#!/usr/bin/env python3
"""Make webui/board_data.js from the KiCad board of the badge.

The board is hackerhotel2027.kicad_pcb in the public repository
badgeteam/hackerhotel2027-hardware. webui/board_data.js (first committed
2026-11-20 in 076d655) comes from hardware commit HW_REV below: f6015de,
"Prototype 1 (production)". 661f5a2, 0a441be, 8b5bb00 and d4e221e give
the same data; 333f569 and older do not (SW2 and SW3 turned 180 degrees,
other LEDs). The lists keep the board file's order.

For new hardware: set HW_REV to the new commit (full SHA), run this
script, then tools/gen_layout.py, and commit board_data.js and the
tables together (see "Layout tables" in CONTRIBUTING.md).

    python3 tools/extract_board_data.py [BOARD] [--rev REV] [-o OUT]

BOARD is the hardware repository or a .kicad_pcb file. Without it the
script uses $HH27_HW (and only that, when it is set), else looks for
hackerhotel2027-hardware next to this repository or one level up, as the
old firmware's tools/gen_layout.py did. In a git repository it reads the
board of commit REV (default HW_REV) with `git show`, so it does not
matter what is checked out there. A .kicad_pcb file is read as it is.
OUT is webui/board_data.js by default, "-" for standard output.

Pure Python 3: it parses the S-expressions itself and needs no pcbnew and
no kicad-cli, so CI can run it. tools/board_test/run.sh checks the result
against webui/board_data.js.

How the LEDs are wired (traced from the board, not assumed):
- The pin function of a CH643 (U1) pad, "PA15/..." etc., gives the port
  pin of its net. 2-pad series resistors are followed to the MCU.
- An LED anode (pad 2) reaches a PA pin: that is the PWM column (col).
- An LED cathode reaches a PB pin: that is the COM row (row).
- 0402 LEDs are mono (cathode pad 1). B1515 LEDs are common-anode RGB:
  pads 1, 4 and 3 are the red (row), green and blue cathodes.
An LED that does not reach a PA and a PB pin goes in "undriveable".

Positions are the footprints' (at x y rot), raw KiCad mm. The outline is
the points of the biggest Edge.Cuts gr_poly (not shapes inside
footprints); gr_circle holes are left out. Every revision of the board
draws its edge like that. Any other Edge.Cuts shape (gr_line, gr_arc,
gr_rect, gr_curve, an arc in a gr_poly) stops the script with an error,
so a redrawn edge fails loudly. Buttons are the SW footprints, labelled
with the net of their non-GND pad.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PCB_NAME = "hackerhotel2027.kicad_pcb"
HW_REV = "f6015de891c1a95db508acfdfb9fb68b8be4fe7c"  # CI fetches this commit
RGB_PADS = (("row", "1"), ("green_row", "4"), ("blue_row", "3"))

TOKEN = re.compile(r'\(|\)|"((?:[^"\\]|\\.)*)"|[^\s()"]+')


def parse(text):
    """S-expression text to nested lists of strings."""
    stack, cur = [], []
    for m in TOKEN.finditer(text):
        t = m.group(0)
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done, cur = cur, stack.pop()
            cur.append(done)
        elif m.group(1) is not None:
            cur.append(m.group(1).replace('\\"', '"').replace("\\\\", "\\"))
        else:
            cur.append(t)
    return cur[0]


def kids(node, name):
    return [n for n in node if isinstance(n, list) and n and n[0] == name]


def kid(node, name):
    k = kids(node, name)
    return k[0] if k else None


def xy(node):
    return (float(node[1]), float(node[2]))


# ------------------------------------------------------------- board ---

def find_board(arg):
    name = "hackerhotel2027-hardware"
    if arg or os.environ.get("HH27_HW"):  # asked for: no fallback to another board
        cands = [arg or os.environ["HH27_HW"]]
    else:
        up = os.path.dirname(REPO)
        cands = [os.path.join(up, name), os.path.join(os.path.dirname(up), name)]
    for c in cands:
        if os.path.isfile(c):
            return c
        if os.path.isfile(os.path.join(c, PCB_NAME)) or os.path.exists(os.path.join(c, ".git")):
            return c
    return None


def read_board(where, rev):
    """The board's text and where it came from, for the header."""
    if os.path.isfile(where):
        return open(where, encoding="utf-8").read(), os.path.basename(where)
    if os.path.exists(os.path.join(where, ".git")):
        git = subprocess.run(["git", "-C", where, "show", f"{rev}:{PCB_NAME}"], capture_output=True)
        if git.returncode:
            sys.exit(f"{where}: no {PCB_NAME} at {rev} (git fetch?): {git.stderr.decode().strip()}")
        text = git.stdout.decode("utf-8")
        short = rev[:7] if re.fullmatch(r"[0-9a-f]{40}", rev) else rev
        return text, f"hackerhotel2027-hardware {short}:{PCB_NAME}"
    return open(os.path.join(where, PCB_NAME), encoding="utf-8").read(), PCB_NAME


# --------------------------------------------------------------- LEDs ---

def footprints(board):
    for fp in kids(board, "footprint"):
        ref = ""
        for p in kids(fp, "property"):
            if p[1] == "Reference":
                ref = p[2]
        for t in kids(fp, "fp_text"):  # KiCad 7 and older
            if t[1] == "reference":
                ref = t[2]
        at = kid(fp, "at")
        pads = {}
        func = {}
        for pad in kids(fp, "pad"):
            net = kid(pad, "net")
            pf = kid(pad, "pinfunction")
            if net and net[-1]:
                pads[pad[1]] = net[-1]  # (net "name") or (net 12 "name")
                func[net[-1]] = pf[1] if pf else ""
        yield dict(lib=fp[1], ref=ref, x=float(at[1]), y=float(at[2]),
                   rot=float(at[3]) if len(at) > 3 else 0.0, pads=pads, func=func)


def wiring(fps):
    """Net to MCU port pin, through 2-pad series resistors."""
    pin = {}
    for fp in fps:
        if fp["ref"] == "U1":
            for net, f in fp["func"].items():
                m = re.match(r"P([ABC])(\d+)", f)
                if m:
                    pin[net] = (m.group(1), int(m.group(2)))
    for fp in fps:
        if fp["ref"].startswith("R") and len(fp["pads"]) == 2:
            a, c = fp["pads"].values()
            if a in pin and c not in pin:
                pin[c] = pin[a]
            elif c in pin and a not in pin:
                pin[a] = pin[c]
    return pin


def extract(board):
    fps = list(footprints(board))
    pin = wiring(fps)

    def port(net, want):
        p = pin.get(net)
        return p[1] if p and p[0] == want else None

    leds, rgb, bad, buttons = [], [], [], []
    for fp in fps:
        place = dict(ref=fp["ref"], x=fp["x"], y=fp["y"], rot=fp["rot"])
        pads = fp["pads"]
        if fp["ref"].startswith("D") and ("0402" in fp["lib"] or "B1515" in fp["lib"]):
            col = port(pads.get("2"), "A")
            rgb_led = "B1515" in fp["lib"]
            rows = {k: port(pads.get(n), "B") for k, n in (RGB_PADS if rgb_led else RGB_PADS[:1])}
            if col is None or None in rows.values():
                bad.append(place)
            elif rgb_led:
                rgb.append(dict(place, row=rows["row"], col=col, green_row=rows["green_row"],
                                blue_row=rows["blue_row"]))
            else:
                leds.append(dict(place, row=rows["row"], col=col))
        elif fp["ref"].startswith("SW"):
            nets = [n for n in pads.values() if n != "GND" and not n.startswith("unconnected")]
            buttons.append(dict(ref=fp["ref"], label=nets[0] if nets else fp["ref"],
                                x=fp["x"], y=fp["y"], rot=fp["rot"]))
    buttons.sort(key=lambda b: int(re.sub(r"\D", "", b["ref"]) or 0))
    return leds, rgb, bad, buttons


# ------------------------------------------------------------ outline ---

def area(pts):
    return abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))) / 2


def outline(board):
    """The biggest Edge.Cuts gr_poly; gr_circle holes are left out."""
    polys = []
    for g in board:
        if not (isinstance(g, list) and g and g[0].startswith("gr_")):
            continue
        layer = kid(g, "layer")
        if not layer or layer[1] != "Edge.Cuts" or g[0] == "gr_circle":
            continue
        pts = kid(g, "pts") if g[0] == "gr_poly" else None
        if pts is None or kids(pts, "arc"):
            sys.exit(f"Edge.Cuts has a {g[0]}{' with arcs' if pts else ''}: this script only "
                     "reads gr_poly shapes of xy points (and leaves gr_circle holes out); "
                     "extend outline() in tools/extract_board_data.py")
        polys.append([xy(p) for p in kids(pts, "xy")])
    if not polys:
        sys.exit("no Edge.Cuts gr_poly: can't find the board outline")
    return [[round(x, 6), round(y, 6)] for x, y in max(polys, key=area)]


# -------------------------------------------------------------- output ---

def board_data(text):
    board = parse(text)
    leds, rgb, bad, buttons = extract(board)
    return {"outline": outline(board), "leds": leds, "rgb_leds": rgb,
            "undriveable": bad, "buttons": buttons}


def render(data, source):
    return ("// Generated by tools/extract_board_data.py -- do not hand-edit.\n"
            f"// From {source}.\n"
            "// Board (mm) coordinates; see docs/LEDPWM_MATRIX.md for row/col addressing.\n"
            f"const BOARD_DATA = {json.dumps(data)};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("board", nargs="?", help="hardware repository or .kicad_pcb file")
    ap.add_argument("--rev", default=HW_REV, help=f"hardware commit to read (default {HW_REV[:7]})")
    ap.add_argument("-o", "--out", default=os.path.join(REPO, "webui", "board_data.js"))
    args = ap.parse_args()
    where = find_board(args.board)
    if not where:
        sys.exit("can't find hackerhotel2027-hardware: clone it next to this repository, "
                 "set HH27_HW or pass the repository or the .kicad_pcb")
    text, source = read_board(where, args.rev)
    data = board_data(text)
    out = render(data, source)
    if args.out == "-":
        sys.stdout.write(out)
    else:
        with open(args.out, "w") as f:
            f.write(out)
    print(f"{args.out}: {len(data['leds'])} mono, {len(data['rgb_leds'])} RGB, "
          f"{len(data['undriveable'])} undriveable, {len(data['buttons'])} buttons, "
          f"{len(data['outline'])} outline points, from {source}", file=sys.stderr)


if __name__ == "__main__":
    main()
