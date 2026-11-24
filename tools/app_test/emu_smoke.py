#!/usr/bin/env python3
"""Every app in the emulator's RISC-V interpreter (tools/emu): stored on a
fresh badge, it must start, draw, take 4 s of random button taps (B, C, D,
and short taps of A) and still run, with no fault in the log ("[emu]": a
bad pointer, a jump outside the app, a tick() that does not return).

  python3 tools/app_test/emu_smoke.py EMU APP.hhapp...
"""
import os
import queue
import random
import subprocess
import sys
import tempfile
import threading
import time

MODE_APP = 3


def smoke(emu, path):
    data = open(path, "rb").read()
    name = os.path.splitext(os.path.basename(path))[0].upper()
    with tempfile.TemporaryDirectory() as tmp:
        p = subprocess.Popen([emu, os.path.join(tmp, "flash.bin")], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             text=True, bufsize=1)
        replies, faults, frames = queue.Queue(), [], [0]

        def read():
            for line in p.stdout:
                if line.startswith("U "):
                    replies.put(bytes.fromhex(line[2:].strip()))
                elif line.startswith("L [emu]"):
                    faults.append(line[2:].strip())
                elif line.startswith("F "):
                    frames[0] += 1

        threading.Thread(target=read, daemon=True).start()

        def send(line):
            p.stdin.write(line + "\n")
            p.stdin.flush()

        def usb(*b):
            send("u " + bytes(b).hex())
            return replies.get(timeout=10)

        try:
            ok = usb(0x21, 2, *name.encode()[:20])[2] == 0
            for o in range(0, len(data), 60):
                ok = ok and usb(0x22, *data[o:o + 60])[2] == 0
            r = usb(0x23)
            ok = ok and r[2] == 0 and r[0] == MODE_APP
            rnd = random.Random(name)
            end = time.time() + 4
            while time.time() < end and ok:
                send("b %x" % rnd.choice([1, 2, 4, 8, 0]))  # A taps are short: no menu
                time.sleep(rnd.uniform(0.03, 0.12))
                send("b 0")
                time.sleep(rnd.uniform(0.03, 0.2))
            r = usb(0xFF)
            running = r[0] == MODE_APP
        finally:
            p.kill()
            p.wait()
        good = ok and running and not faults and frames[0] > 10
        print("%s  %s: %s, %d frames%s" % ("ok  " if good else "FAIL", name.lower(), "runs" if running else "stopped",
                                           frames[0], "; " + "; ".join(faults[:3]) if faults else ""))
        return good


if __name__ == "__main__":
    emu, apps = sys.argv[1], sys.argv[2:]
    results = [smoke(emu, a) for a in apps]
    sys.exit(0 if all(results) else 1)
