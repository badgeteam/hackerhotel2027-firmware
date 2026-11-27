#!/usr/bin/env python3
"""The badge emulator: runs the firmware on the PC and draws the badge.

  make -C tools/emu
  python3 tools/emu/emu.py [--web] [--flash FILE] [--no-ir] [--fresh [--keep]]

hh27-emu (built by the Makefile) is the real firmware, built for the PC:
the main loop, the item store, the persistent record, the animation
player and the app host. Apps (.hhapp) run in a RISC-V interpreter. This
program starts hh27-emu, draws its LEDs on the badge's outline from
webui/board_data.js, and passes it the buttons.

Keys: A, B, C, D = the buttons (by their silkscreen letters), or click
them on the board. V = USB power in and out. U = the battery: it does not
charge, it charges, or there is none. R = power off and on (buttons held
then are held at power-on: hold one for a safe start). Esc = quit. The
buttons under the board do the same as V and U.

USB power and the battery: the emulated badge starts on USB power, with a
battery that does not charge. The firmware reads them as on the badge
(power.c): PC3 high while USB power is in, and the charger's CHRG (PB22)
low while the battery charges. Without USB power the charger is off, and
the badge gets no USB packets (the web interface then shows it as
disconnected, as when a badge is unplugged; Connect emulator again after V).
Without a battery, CHRG toggles every 100 ms, as the charger does when no
battery is in. (A real badge with neither is off; the emulator runs on.)

The flash (items, name, settings) is kept in a file, by default
~/.cache/hh27-emu/flash.bin, so it survives R and a restart. The
firmware's log appears in this terminal.

--fresh starts as a badge newly flashed with the factory image (make
factory-image), but with the items kept: the persistent record (the name,
the settings, the boot item, whether the factory test passed, and what
apps saved, such as high scores) erased, and the factory test app
(apps/build/factory.hhapp, make -C apps) stored after the items, so the
factory test runs at power-on. It runs on a copy of the flash file,
which goes when the emulator closes: your flash file stays as it was.
With --keep, it changes the flash file itself, and keeps what happens
after. In both, R runs the factory test again until it passes, as on the
badge; after the pass, the app deletes itself. --fresh --flash
factory.bin starts from the factory image itself (all 62 KB, on a copy):
the store empty but for the test. tools/badge.py factory also stores the
test and starts it.

--web serves the web interface (webui/) on http://127.0.0.1:8027/ (--port
to change) and opens it (--no-browser not). Click "Connect emulator" there: the page then talks to the
emulator as it talks to a badge over WebUSB. Any browser works, not only
the ones with WebUSB.

Infrared: every emulator on this PC shares one "room" (UDP broadcast on
127.255.255.255:8028), so apps on two emulators can reach each other.
--no-ir leaves the room.
"""
import argparse
import json
import os
import queue
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import tkinter as tk
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
EMU = HERE / "build" / "hh27-emu"
WEB_PORT = 8027
IR_ADDR = ("127.255.255.255", 8028)

# bit in the "b" message, by silkscreen letter (apps/hhapp.h HH_BTN_*), and
# the switch on the board
BUTTONS = {"A": (0, "SW1"), "C": (1, "SW3"), "B": (2, "SW2"), "D": (3, "SW4")}

SIZE = 640  # window, pixels
PCB = "#1d2b22"
OFF = (48, 50, 52)
MONO = (255, 236, 205)


def load_board():
    s = (REPO / "webui" / "board_data.js").read_text()
    return json.loads(s[s.index("{"):s.rindex("}") + 1])


class Badge:
    """hh27-emu as a child process; its lines go to a queue."""

    def __init__(self, flash, held=0):
        self.p = subprocess.Popen([str(EMU), str(flash), "%x" % held],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines = queue.Queue()
        self.usb_replies = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            line = line.rstrip("\n")
            if line.startswith("U "):
                self.usb_replies.put(bytes.fromhex(line[2:]))
            else:
                self.lines.put(line)
        self.lines.put(None)  # the program ended

    def send(self, line):
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def stop(self):
        if self.p.poll() is None:
            self.p.kill()
        self.p.wait()


class WebBridge:
    """The web interface and a /usb endpoint on localhost. One packet at a
    time, as over WebUSB: the page waits for each reply."""

    def __init__(self, app, port):
        self.lock = threading.Lock()
        bridge = self

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *a, **kw):
                super().__init__(*a, directory=str(REPO / "webui"), **kw)

            def do_GET(self):
                if self.path.rstrip("/") == "/emu":  # tells the page an emulator is here
                    return self._reply(b"hh27-emu", "text/plain")
                return super().do_GET()

            def do_POST(self):
                if self.path.rstrip("/") != "/usb":
                    return self.send_error(404)
                packet = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                if not app.vbus:
                    return self.send_error(503, "USB is unplugged in the emulator (V plugs it in)")
                reply = bridge.usb(app, packet)
                if reply is None:
                    return self.send_error(504, "the emulator did not reply")
                self._reply(reply, "application/octet-stream")

            def _reply(self, body, kind):
                self.send_response(200)
                self.send_header("Content-Type", kind)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a):
                pass

        try:
            self.server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        except OSError as e:
            sys.exit("port %d: %s (another server there? try --port)" % (port, e.strerror))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def usb(self, app, packet):
        with self.lock:
            badge = app.badge
            if not badge or not packet:
                return None
            while not badge.usb_replies.empty():  # a late reply from an earlier packet
                badge.usb_replies.get_nowait()
            badge.send("u " + packet[:64].hex())
            try:
                return badge.usb_replies.get(timeout=5)  # erasing the store takes a moment
            except queue.Empty:
                return None


class IrRoom:
    """The emulators' shared infrared: a UDP broadcast only this PC sees.
    Each packet starts with a random sender id, so an emulator skips its own."""

    def __init__(self):
        self.me = os.urandom(4)
        self.sock = s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.bind(IR_ADDR)
        s.setblocking(False)

    def send(self, address, command):
        self.sock.sendto(self.me + b"I" + bytes([address & 0xFF, address >> 8, command]), IR_ADDR)

    def send_packet(self, packet):
        self.sock.sendto(self.me + b"P" + packet, IR_ADDR)

    def received(self):
        """("i <address> <command>" or "p <hex>") lines for hh27-emu"""
        while True:
            try:
                data = self.sock.recv(64)
            except BlockingIOError:
                return
            if data[:4] == self.me:
                continue
            if data[4:5] == b"I" and len(data) == 8:
                yield "i %d %d" % (data[5] | data[6] << 8, data[7])
            elif data[4:5] == b"P":
                yield "p " + data[5:].hex()


# the battery (U), as hw.c's "c" line takes it
BATTERY = ["does not charge", "charges", "none"]


class App:
    def __init__(self, flash, web, ir, port=WEB_PORT, browser=True):
        self.flash, self.ir = flash, ir
        self.vbus, self.battery = True, 0
        self.board = board = load_board()
        self.root = root = tk.Tk()
        root.title("Hacker Hotel 2027 badge emulator")
        root.configure(bg="black")
        self.canvas = c = tk.Canvas(root, width=SIZE, height=SIZE, bg="black", highlightthickness=0)
        c.pack()
        bar = tk.Frame(root, bg="black")
        bar.pack(fill="x")
        style = dict(takefocus=0, bg="#222", fg="#ddd", activebackground="#444", activeforeground="#fff",
                     highlightthickness=0, font=("sans", 10))
        self.vbus_btn = tk.Button(bar, command=self.toggle_vbus, **style)
        self.vbus_btn.pack(side="left", padx=4, pady=2)
        self.battery_btn = tk.Button(bar, command=self.next_battery, **style)
        self.battery_btn.pack(side="left", padx=4, pady=2)
        self.status = tk.Label(root, text="", fg="#bbb", bg="black", font=("sans", 10), anchor="w", width=1)
        self.status.pack(fill="x")

        xs = [p[0] for p in board["outline"]]
        ys = [p[1] for p in board["outline"]]
        span = max(max(xs) - min(xs), max(ys) - min(ys))
        self.scale = (SIZE - 40) / span
        self.ox = SIZE / 2 - (min(xs) + max(xs)) / 2 * self.scale
        self.oy = SIZE / 2 - (min(ys) + max(ys)) / 2 * self.scale

        c.create_polygon([v for p in board["outline"] for v in self.px(*p)], fill=PCB, outline="#3c5a46", width=2)
        self.dots = {}  # cell -> canvas item, for the mono LEDs
        for led in board["leds"]:
            x, y = self.px(led["x"], led["y"])
            r = max(2.0, 0.55 * self.scale)
            self.dots[led["row"] * 24 + led["col"]] = c.create_rectangle(x - r, y - r, x + r, y + r, width=0, fill=hexcol(OFF))
        self.rgb = []  # (item, red cell, green cell, blue cell)
        for led in board["rgb_leds"]:
            x, y = self.px(led["x"], led["y"])
            r = max(4.0, 1.4 * self.scale)
            item = c.create_oval(x - r, y - r, x + r, y + r, width=1, outline="#555", fill=hexcol(OFF))
            self.rgb.append((item, led["row"] * 24 + led["col"], led["green_row"] * 24 + led["col"], led["blue_row"] * 24 + led["col"]))

        self.held = 0
        self.button_items = {}
        refs = {b["ref"]: b for b in board.get("buttons", [])}
        for letter, (bit, ref) in BUTTONS.items():
            b = refs.get(ref)
            if not b:
                continue
            x, y = self.px(b["x"], b["y"])
            r = max(10.0, 3.0 * self.scale)
            item = c.create_oval(x - r, y - r, x + r, y + r, fill="#333", outline="#888", width=2)
            text = c.create_text(x, y, text=letter, fill="#ddd", font=("sans", 12, "bold"))
            self.button_items[letter] = item
            for i in (item, text):
                c.tag_bind(i, "<ButtonPress-1>", lambda e, l=letter: self.press(l))
                c.tag_bind(i, "<ButtonRelease-1>", lambda e, l=letter: self.release(l))
        self.release_timers = {}
        root.bind("<KeyPress>", self.key_down)
        root.bind("<KeyRelease>", self.key_up)
        root.protocol("WM_DELETE_WINDOW", self.quit)

        print("flash: %s" % flash)
        self.room = IrRoom() if ir else None
        self.state = "on"
        self.badge = None
        self.power_on()
        if web:
            self.web = WebBridge(self, port)
            url = "http://127.0.0.1:%d/" % port
            print("web interface: %s (click Connect emulator)" % url)
            if browser:
                webbrowser.open(url)
        root.after(15, self.tick)

    def px(self, x, y):
        return self.ox + x * self.scale, self.oy + y * self.scale

    # --- the badge process ---

    def power_on(self):
        if self.badge:
            self.badge.stop()
        self.badge = Badge(self.flash, self.held)
        self.send_power()
        self.state = "on"
        self.show_status()

    def send_power(self):
        if self.badge:
            self.badge.send("v %d" % self.vbus)
            self.badge.send("c %d" % self.battery)

    def toggle_vbus(self):
        self.vbus = not self.vbus
        self.send_power()
        self.show_status()

    def next_battery(self):
        self.battery = (self.battery + 1) % len(BATTERY)
        self.send_power()
        self.show_status()

    def show_status(self):
        what = {"on": "running", "standby": "standby: any button wakes it", "boot": "USB boot ROM (restarting)"}[self.state]
        self.vbus_btn.config(text="V: USB power %s" % ("in" if self.vbus else "out"))
        charger_off = " (charger off: no USB power)" if not self.vbus and BATTERY[self.battery] == "charges" else ""
        self.battery_btn.config(text="U: battery %s%s" % (BATTERY[self.battery], charger_off))
        self.status.config(text="  %s   |   A B C D: buttons   R: power off and on   Esc: quit" % what)

    def tick(self):
        badge = self.badge
        while badge:
            try:
                line = badge.lines.get_nowait()
            except queue.Empty:
                break
            if line is None:  # it exited: the boot ROM, or a crash
                if self.state != "boot":
                    print("hh27-emu stopped (exit code %s)" % badge.p.poll())
                    self.state = "boot"
                    self.show_status()
                self.root.after(1000, self.power_on)
                self.badge = badge = None
                break
            kind, _, rest = line.partition(" ")
            if kind == "F":
                self.draw(bytes.fromhex(rest))
            elif kind == "L":
                print(rest)
            elif kind == "S":
                self.state = "standby" if rest == "1" else "on"
                self.show_status()
            elif kind == "B":
                self.state = "boot"
                self.show_status()
            elif kind == "I" and self.room:
                a, cmd = map(int, rest.split())
                self.room.send(a, cmd)
            elif kind == "P" and self.room:
                self.room.send_packet(bytes.fromhex(rest))
            elif kind == "R":  # a raw frame: no other badge decodes it, so show it here
                v = list(map(int, rest.split()))
                print("IR raw: %d marks and spaces at %d Hz, %.1f ms" % (len(v) - 1, v[0], sum(v[1:]) / 1000))
        if self.room and self.badge:
            for line in self.room.received():
                self.badge.send(line)
        self.root.after(15, self.tick)

    def draw(self, frame):
        # brightness (global) then the 384 cells, as values a screen shows
        # (the badge applies gamma on its PWM, so this needs none)
        gain = (frame[0] / 255) ** (1 / 2.2)
        cells = frame[1:]
        for cell, item in self.dots.items():
            self.canvas.itemconfigure(item, fill=hexcol(mix(OFF, MONO, cells[cell] / 255 * gain)))
        for item, r, g, b in self.rgb:
            lit = [cells[r] * gain, cells[g] * gain, cells[b] * gain]
            m = max(lit) / 255
            self.canvas.itemconfigure(item, fill=hexcol(tuple(int(OFF[i] * (1 - m) + lit[i]) for i in range(3))))

    # --- buttons ---

    def press(self, letter):
        self.held |= 1 << BUTTONS[letter][0]
        self.canvas.itemconfigure(self.button_items.get(letter, ""), fill="#7a7")
        self.send_buttons()

    def release(self, letter):
        self.held &= ~(1 << BUTTONS[letter][0])
        self.canvas.itemconfigure(self.button_items.get(letter, ""), fill="#333")
        self.send_buttons()

    def send_buttons(self):
        if self.badge:
            self.badge.send("b %x" % self.held)

    def key_down(self, e):
        key = e.keysym.upper()
        if key in BUTTONS:
            timer = self.release_timers.pop(key, None)
            if timer:  # key repeat: a release and a press straight after
                self.root.after_cancel(timer)
            elif not self.held & (1 << BUTTONS[key][0]):
                self.press(key)
        elif key == "V":
            self.toggle_vbus()
        elif key == "U":
            self.next_battery()
        elif key == "R":
            self.power_on()
        elif key == "ESCAPE":
            self.quit()

    def key_up(self, e):
        key = e.keysym.upper()
        if key in BUTTONS:
            self.release_timers[key] = self.root.after(40, lambda: (self.release_timers.pop(key, None), self.release(key)))

    def quit(self):
        if self.badge:
            self.badge.stop()
        self.root.destroy()


def mix(base, col, amount):
    """from base towards col, amount 0..1"""
    amount = max(0.0, min(1.0, amount))
    return tuple(int(base[i] + (col[i] - base[i]) * amount) for i in range(3))


def hexcol(c):
    return "#%02x%02x%02x" % c


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--web", action="store_true", help="serve the web interface and let it talk to the emulator")
    p.add_argument("--flash", default=str(Path.home() / ".cache" / "hh27-emu" / "flash.bin"), help="the flash file")
    p.add_argument("--port", type=int, default=WEB_PORT, help="port for --web (default %d)" % WEB_PORT)
    p.add_argument("--no-browser", action="store_true", help="with --web: do not open the browser")
    p.add_argument("--no-ir", action="store_true", help="stay out of the shared infrared room")
    p.add_argument("--fresh", action="store_true",
                   help="start as a newly flashed badge (the record erased, the factory test app stored), on a copy of the flash file")
    p.add_argument("--keep", action="store_true", help="with --fresh: erase the record (name, settings, apps' saved data) and store the factory test in the flash file itself, and keep the changes")
    args = p.parse_args()
    if args.keep and not args.fresh:
        p.error("--keep goes with --fresh")
    if not EMU.exists():
        sys.exit("%s is missing: run make -C tools/emu first" % EMU)
    Path(args.flash).parent.mkdir(parents=True, exist_ok=True)
    flash, scratch = args.flash, None
    if args.fresh and not args.keep:  # a copy, so the flash file stays as it was
        scratch = tempfile.mkdtemp(prefix="hh27-emu-")
        flash = os.path.join(scratch, "flash.bin")
        if os.path.exists(args.flash):
            shutil.copyfile(args.flash, flash)
        print("fresh start on a copy of %s" % args.flash)
        for s in (signal.SIGTERM, signal.SIGHUP):  # killed or the terminal closed: still remove the copy
            signal.signal(s, lambda *a: sys.exit(0))
    try:
        if args.fresh:
            sys.path.insert(0, str(REPO / "tools"))
            import factory_image
            if not os.path.exists(factory_image.APP):
                sys.exit("%s is missing: run make -C apps first" % factory_image.APP)
            if not factory_image.fresh(flash, open(factory_image.APP, "rb").read()):
                print("no room in the store for the factory test app: the record is erased, the test does not run")
        App(flash, args.web, not args.no_ir, args.port, not args.no_browser).root.mainloop()
    finally:
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)


main()
