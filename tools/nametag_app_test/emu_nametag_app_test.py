#!/usr/bin/env python3
"""The NAMETAG app (apps/nametag) on emulated badges linked by infrared
(tools/emu/headless.py): two badges running it learn each other's names
by themselves and say hi; C shows the panel and the dots of who is near;
the hackers met survive a power cycle; a badge with this firmware and no
app answers no HELLO and drops the packets, so its next app does not get
them; a badge with older firmware (a model of its SAY HI) answers the
app's HELLO, and the app answers its SAY HI in time; banana shots count
and colour the arms; the style is kept; a friend heard under the
charging screen still counts and says hi once a tap hides it (the
charging screen itself: tools/charge_test).

  python3 tools/nametag_app_test/emu_nametag_app_test.py APP.hhapp
"""
import os
import random
import re
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

TYPE_APP = 2

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
        time.sleep(0.05)
    return False


def load_layout():
    """layout.c's outline slots and RGB LEDs (r, g, b, arm, rank)"""
    src = open(os.path.join(ROOT, "layout.c")).read()
    edge = src[src.index("layout_edge[HH_EDGE_COUNT] = {"):]
    edge = [int(v) for v in re.findall(r"\d+", edge[edge.index("{") + 1:edge.index("}")])]
    rgb = src[src.index("layout_rgb[HH_RGB_COUNT] = {"):]
    rgb = [tuple(int(v) for v in m) for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}", rgb)][:35]
    assert len(edge) == 71 and len(rgb) == 35, "layout.c: expected 71 outline and 35 RGB LEDs"
    return edge, rgb


EDGE, RGB = load_layout()


def shows(emu, text):
    """The grid shows text, still and centred or scrolling"""
    frame = emu.wait_frames()
    return shows_still(frame, text) or scroll_offset(frame, text) is not None


def logged(emu, text):
    return any(text in line for line in emu.log)


def rgb(frame, led):
    r, g, b = RGB[led][:3]
    return frame[r], frame[g], frame[b]


def battery(frame):
    """The charging screen: the battery's outline (x 4 .. 12, y 3 .. 14) lit,
    the outline LEDs dark"""
    ring = [GRID[y][x] for y in range(3, 15) for x in range(4, 13) if y in (3, 14) or x in (4, 12)]
    return all(frame[s] for s in ring) and not any(frame[s] for s in EDGE)


class OldFirmwareBadge:
    """A badge with the firmware from before SAY HI was dropped (greet.c,
    which the NAMETAG app must keep working with), linked like an Emu
    (link()). Packets: type 'H' (HELLO) or 'R' (REPLY), the name as data,
    no NUL. It answers every HELLO with a REPLY after 20 .. 270 ms and
    shows "HI <name>" (up to a NUL); SAY HI sends a HELLO every 1.5 s, 4
    at most, until a REPLY comes, and shows that name."""

    def __init__(self, name):
        self.name = name.encode()
        self.peers = []
        self.shown = []        # what it showed: "HI <name>"
        self.hellos = 0        # HELLOs sent by SAY HI
        self.hello_t = None    # when the last of them went out
        self.answer = None     # SAY HI's answer: (seconds after that HELLO, the packet)
        self.calling = False
        self.lock = threading.Lock()

    def _show(self, data):
        self.shown.append("HI " + (data.split(b"\0")[0].decode("latin-1") or "!"))

    def _say(self, t):
        packet = bytes([ord(t), len(self.name)]) + self.name
        for peer in self.peers:
            peer.send("p " + packet.hex())

    def send(self, line):  # a packet from a linked emulator: "p <type, length, data>"
        p = bytes.fromhex(line.split()[1])
        with self.lock:
            if p[0] == ord("H"):
                self._show(p[2:])
                threading.Timer(random.uniform(0.02, 0.27), self._say, ("R",)).start()
            elif p[0] == ord("R") and self.calling:
                self.calling = False
                self.answer = (time.time() - self.hello_t, p)
                self._show(p[2:])

    def say_hi(self):
        def call():
            for _ in range(4):
                with self.lock:
                    if not self.calling:
                        return
                    self.hellos += 1
                    self.hello_t = time.time()
                    self._say("H")
                time.sleep(1.5)
            self.calling = False
        self.calling, self.hellos, self.answer = True, 0, None
        threading.Thread(target=call, daemon=True).start()


def air_ms(length):
    """A packet with length data bytes on air, at the slowest (hhapp.h)"""
    return 7 + 18 * (length + 3)


def install(emus, app):
    """Stores the app on all of them at once, so they start together"""
    results = {}
    threads = [threading.Thread(target=lambda e=e: results.__setitem__(e, e.store(TYPE_APP, "NAMETAG", app))) for e in emus]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return all(results[e][2] == 0 for e in emus)


def restart(emu, flash, peers):
    """Power off and on: the same flash; the boot item (NAMETAG) starts"""
    emu.stop()
    for p in peers:
        p.peers = [q for q in p.peers if q is not emu]
    new = Emu(flash)
    for p in peers:
        link(new, p)
    new.wait_frames()
    return new


def main():
    app = open(sys.argv[1], "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        paths = [os.path.join(tmp, n + ".bin") for n in "abc"]
        emus = [Emu(p) for p in paths]
        try:
            run(app, paths, emus)
        finally:
            for e in emus:
                e.stop()
    print("%s  NAMETAG app: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


def run(app, paths, emus):
    a, b, c = emus
    for e in emus:
        e.wait_frames()
    a.set_name("ALICE")
    b.set_name("BOB")
    c.set_name("CAROL")
    time.sleep(1.5)  # the names are written (persist.c)

    # E1: two badges running it learn each other's names by themselves
    link(a, b)
    check(install([a, b], app), "E1 the app is stored and runs on both")
    check(wait_for(lambda: logged(a, "nametag: new BOB") and logged(b, "nametag: new ALICE"), 8),
          "E1 within 8 s A logs BOB and B logs ALICE")
    check(wait_for(lambda: shows(a, "HI BOB"), 3), "E1 A shows HI BOB")
    check(wait_for(lambda: shows_still(b.wait_frames(), "BOB"), 12), "E1 then B's name tag BOB returns")
    check(wait_for(lambda: shows(a, "ALICE"), 8), "E1 and A's ALICE (it scrolls)")
    sent = [p for p in a.ir_sent if p[0] == ord("N")]
    check(sent and sent[0] == b"N\x07ALICE\x00\x1f", "E1 A's beacon 'N': its name, a NUL, no team (%r)" % sent[:1])

    # E2: C opens the panel, a dot goes round for B
    a.press(BTN_C)
    check(wait_for(lambda: scroll_offset(a.wait_frames(), "MET 1 NEAR 1 BOB") is not None, 3), "E2 C: the panel MET 1 NEAR 1 BOB")
    check(wait_for(lambda: (lambda f: any(f[s] == 255 for s in EDGE))(a.wait_frames()), 2),
          "E2 a dot on the outline for the badge nearby")
    a.press(BTN_C)
    t_hello = time.time()

    # E3: a power cycle keeps the hackers met
    time.sleep(3.5)  # the save is flushed
    a = emus[0] = restart(a, paths[0], [b])
    check(wait_for(lambda: logged(a, "nametag: style 0 met 1"), 5), "E3 after a power cycle: NAMETAG starts, met 1")
    time.sleep(16)  # A's first HELLO (5 .. 15 s) gets B's answer
    check(not logged(a, "nametag: new BOB"), "E3 BOB is not new again")
    a.press(BTN_C)
    check(wait_for(lambda: scroll_offset(a.wait_frames(), "MET 1 NEAR 1 BOB") is not None, 3), "E3 C: MET 1 NEAR 1 BOB")
    a.press(BTN_C)
    t_hello = time.time()

    # E4: a badge with this firmware and no app: no answer, no greeting
    a.peers.clear()
    b.peers.clear()
    link(a, c)
    time.sleep(max(0, t_hello + 5.5 - time.time()))  # C sends HELLO only 5 s after the last
    sent = len(a.ir_sent)
    a.press(BTN_C)
    t_hello = time.time()
    check(wait_for(lambda: any(p[0] == ord("H") for p in a.ir_sent[sent:]), 2), "E4 C on A sends a HELLO")
    check(not wait_for(lambda: c.ir_sent, 2.5), "E4 the plain badge answers no HELLO (%r)" % c.ir_sent[:1])
    check(shows(c, "CAROL"), "E4 and its name tag stays")
    a.press(BTN_C)
    # The packets it got wait for no app: one that starts later learns no
    # name from them
    a.peers.clear()
    c.peers.clear()
    c.send("p " + b"H\x03BOB".hex())
    time.sleep(0.5)
    check(install([c], app), "E4 then the app starts on the plain badge")
    check(not wait_for(lambda: logged(c, "nametag: new"), 3),
          "E4 and gets no packet from before it (%r)" % [line for line in c.log if "nametag: new" in line])

    # E4: a badge with older firmware, which has SAY HI
    old = OldFirmwareBadge("DAVE")
    link(a, old)
    time.sleep(max(0, t_hello + 5.5 - time.time()))
    a.press(BTN_C)
    check(wait_for(lambda: old.shown, 2) and old.shown == ["HI ALICE"],
          "E4 C on A: the older badge shows HI ALICE, not the tail (%r)" % old.shown)
    check(wait_for(lambda: logged(a, "nametag: new DAVE"), 2), "E4 and A learns DAVE from its answer")
    a.press(BTN_C)
    time.sleep(1)
    old.say_hi()
    check(wait_for(lambda: old.answer, 2.5), "E4 SAY HI on the older badge: A answers")
    if old.answer:
        ms, p = old.answer[0] * 1000, old.answer[1]
        check(p == b"R\x07ALICE\x00\x1f" and old.shown[-1] == "HI ALICE",
              "E4 a REPLY with A's name and tail, the older badge shows HI ALICE (%r, %r)" % (p, old.shown[-1:]))
        # It must end before that badge calls again, 1.5 s after its HELLO
        # began (the emulator delivers a packet when it starts)
        end = air_ms(len(old.name)) + ms + air_ms(p[1])
        check(old.hellos == 1 and end < 1500, "E4 one HELLO: the REPLY ends %d ms after it began" % end)
    time.sleep(5)  # A's popups are over

    # E5: the banana
    a.peers.clear()
    c.peers.clear()
    time.sleep(1)
    a.shot(0, SHOT_SHOOT)
    red = []
    t0 = time.time()
    while time.time() - t0 < 0.9:
        f = a.wait_frames()
        red += [rgb(f, i) for i in range(35) if rgb(f, i)[0] >= 80 and rgb(f, i)[1] == 0 and rgb(f, i)[2] == 0]
    check(len(red) > 0, "E5 a red shot: the arms flash red")
    check(wait_for(lambda: shows_still(a.wait_frames(), "1"), 2), "E5 the hit count 1 shows")
    a.shot(2, SHOT_TEAM)
    time.sleep(2.5)
    f = a.wait_frames()
    tips = [rgb(f, i) for i in range(35) if RGB[i][4] == 6]
    check(all(t[0] == 0 and t[2] > 150 and t[1] < t[2] for t in tips), "E5 a banana's team 2: the tips are blue %r" % tips)

    # E6: the style is kept over a power cycle
    a.press(BTN_D)
    a.press(BTN_D)
    time.sleep(3.5)
    a = emus[0] = restart(a, paths[0], [])
    check(wait_for(lambda: logged(a, "nametag: style 5 met 2"), 5), "E6 D twice: style 5 (GLITCH) after a power cycle, met 2 (%r)" %
          [line for line in a.log if "nametag" in line])

    # E7: the battery charges: the charging screen; a new friend under it
    # counts, and says hi once a tap hides it
    a.charging(True)
    check(wait_for(lambda: battery(a.wait_frames()), 3), "E7 charging starts: the battery")
    a.send("p " + b"N\x06ERIN\x00\x1f".hex())  # a beacon from a NAMETAG badge
    check(wait_for(lambda: logged(a, "nametag: new ERIN"), 2), "E7 under it a beacon: A learns ERIN")
    check(battery(a.wait_frames()), "E7 the battery stays")
    a.press(BTN_C)
    check(wait_for(lambda: shows(a, "HI ERIN"), 2), "E7 C hides it: HI ERIN")


if __name__ == "__main__":
    main()
