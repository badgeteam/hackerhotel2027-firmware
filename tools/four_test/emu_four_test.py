#!/usr/bin/env python3
"""4 in a row on two emulated badges linked by infrared
(tools/emu/headless.py): they pair up by themselves, a whole game is
played with the buttons, both boards agree, both badges log the same
moves and the right result; a game against the badge; the link lost.

  python3 tools/four_test/emu_four_test.py APP.hhapp
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

TYPE_APP = 2
W, H, X0, Y0 = 7, 6, 2, 5  # four.c

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


def board(emu):
    """{(r, c): brightness} of the discs on the grid (the last disc may be
    blinking off)"""
    f = emu.wait_frames()
    return {(r, c): f[GRID[Y0 + r][X0 + 2 * c]] for r in range(H) for c in range(W) if f[GRID[Y0 + r][X0 + 2 * c]]}


def discs(emu, count, seconds=4):
    """Waits until the board shows count discs"""
    return wait_for(lambda: len(board(emu)) == count, seconds)


class Player:
    def __init__(self, emu):
        self.emu, self.cursor = emu, 3

    def drop(self, c):
        while self.cursor > c:
            self.emu.press(BTN_C)
            self.cursor -= 1
        while self.cursor < c:
            self.emu.press(BTN_D)
            self.cursor += 1
        self.emu.press(BTN_B)


def logged(emu, text):
    return any(text in line for line in emu.log)


def main():
    app = open(sys.argv[1], "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        emus = [Emu(os.path.join(tmp, "%d.bin" % i)) for i in range(3)]
        try:
            run(app, *emus)
        finally:
            for e in emus:
                e.stop()
    print("%s  4 in a row: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


def run(app, a, b, c):
    for e in (a, b, c):
        e.wait_frames()
    link(a, b)
    check(a.store(TYPE_APP, "four", app)[2] == 0 and b.store(TYPE_APP, "four", app)[2] == 0, "the app is stored and runs on both")

    # They pair up by themselves
    check(wait_for(lambda: logged(a, "four: linked") and logged(b, "four: linked"), 8), "the two badges pair up")
    first, second = (a, b) if logged(a, "you start") else (b, a)
    check(logged(second, "they start"), "one starts, the other knows it")
    p1, p2 = Player(first), Player(second)

    # A game: the first player gets 0 1 2 3 on the bottom row
    moves = [(p1, 0), (p2, 0), (p1, 1), (p2, 1), (p1, 2), (p2, 2)]
    for k, (p, col) in enumerate(moves):
        p.drop(col)
        check(discs(first, k + 1) and discs(second, k + 1), "move %d (column %d) shows on both" % (k + 1, col + 1))
    time.sleep(1.2)  # the last disc stops blinking
    b1, b2 = board(first), board(second)
    same = b1.keys() == b2.keys() and all((b1[k] == 255) == (b2[k] == 60) for k in b1)
    check(same, "both boards agree, mine bright and theirs dim: %s / %s" % (sorted(b1.items()), sorted(b2.items())))
    p1.drop(3)
    check(wait_for(lambda: logged(first, "four: you win, moves 1122334"), 4), "the first player's badge logs the win and the moves")
    check(wait_for(lambda: logged(second, "four: you lose, moves 1122334"), 4), "the other badge logs the loss and the same moves")

    # The next game: B on both, they pair up again; then the link goes
    first.log.clear()
    second.log.clear()
    time.sleep(3.1)  # the result shows 3 s
    first.press(BTN_B)
    second.press(BTN_B)
    check(wait_for(lambda: logged(first, "four: linked") and logged(second, "four: linked"), 8), "B starts a new game, and they pair up again")
    starter = first if logged(first, "you start") else second
    Player(starter).drop(3)
    check(discs(first, 1) and discs(second, 1), "the first move of the new game arrives")
    a.peers.clear()
    b.peers.clear()
    check(wait_for(lambda: logged(starter, "four: link lost"), 25), "out of reach: after 20 s the game ends with link lost")

    # Against the badge: B before another badge answers
    check(c.store(TYPE_APP, "four", app)[2] == 0, "the app runs on a third badge, alone")
    time.sleep(0.5)
    Player(c).drop(3)
    check(discs(c, 2, 3), "B plays against the badge: it answers with a disc")
    check(not logged(c, "linked"), "without another badge it does not pair up")


if __name__ == "__main__":
    main()
