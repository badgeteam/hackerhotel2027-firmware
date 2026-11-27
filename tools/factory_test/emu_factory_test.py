#!/usr/bin/env python3
"""The factory test (apps/factory) on the emulator (tools/emu/headless.py).

A badge flashed with the factory image (tools/factory_image.py, as `make
factory-image` makes it): the test runs at power-on, at full brightness.
Holding A + B leaves it; it stays in the store and runs again at the next
power-on. Then the whole test: each step's name, LED and ROW going on by
themselves, A opening no menu, KEY failing without all four buttons and B
trying it again, USB (the emulator's USB power and charger switched on and
off), IR on its own echo, OK, then the name tag at the badge's own
brightness, and the app gone from the store. After that nothing runs at
power-on, also when the power went during a test started again
(tools/badge.py factory, which stores the app and keeps the boot item). A
firmware update (a record that never passed, no app) starts nothing. USB
packets get no reply while USB power is out. The emulator's --fresh
(Emu(fresh=True)) erases the record, keeps the items and stores the test.

  HH_FACTORY_APP=factory.hhapp python3 tools/factory_test/emu_factory_test.py
"""
import contextlib
import io
import os
import runpy
import shutil
import sys
import tempfile
import time
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403
import factory_image  # noqa: E402  (headless puts tools/ on the path)

NAME_GET, ITEM_INFO = 0x30, 0x24
TYPE_APP = 2
MODE_APP, MODE_NAMETAG = 3, 4
APP = os.path.join(ROOT, "webui", "apps", "ripple.hhapp")
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


def lit(emu):
    return sum(1 for v in emu.wait_frames() if v)


def logged(emu, text):
    return any(text in line for line in emu.log)


def label(frame, text, y):
    """The brightness of text, centred at row y (7 rows: no descenders), or 0
    if the grid does not show it there. Only the rows of text are looked at."""
    want = text_columns(text)
    x0 = (GRID_W - len(want)) // 2
    top = 0
    for x in range(GRID_W):
        col = want[x - x0] if 0 <= x - x0 < len(want) else 0
        for dy in range(7):
            slot = GRID[y + dy][x] if y + dy < GRID_H else None
            if slot is None:
                continue
            if bool(frame[slot]) != bool(col >> dy & 1):
                return 0
            top = max(top, frame[slot])
    return top


def frames(e, seconds):
    """The LEDs every 10 ms for a while"""
    seen, end = [], time.time() + seconds
    while time.time() < end:
        seen.append(e.wait_frames() if e.frame is None else e.frame[1:])
        time.sleep(0.01)
    return seen


def chg(frame):
    return label(frame, "CHG", 9) > 0  # apps/factory: CHG under USB while the battery charges


def brightness(emu):
    return emu.usb(QUERY)[1]


def items(emu):
    """The stored items' names"""
    return [emu.usb(ITEM_INFO, i)[13:33].rstrip(b"\0").decode() for i in range(emu.usb(QUERY)[3])]


def name_tag(e, seconds):
    return wait_for(lambda: scroll_offset(e.wait_frames(), "ALICE") is not None or shows_still(e.wait_frames(), "ALICE"),
                    seconds)


def badge_py(e, *args):
    """Runs tools/badge.py with args, its USB through the emulator (a
    stand-in for pyusb); returns what it printed, and its exit message"""
    class Dev:
        iSerialNumber = 3

        def is_kernel_driver_active(self, i):
            return False

        def write(self, ep, data, timeout=0):
            self.reply = e.usb(*data)

        def read(self, ep, n, timeout=0):
            return self.reply

    usb = types.ModuleType("usb")
    dev = Dev()
    usb.core = types.SimpleNamespace(find=lambda find_all=False, **kw: [dev] if find_all else dev, USBError=OSError)
    usb.util = types.SimpleNamespace(claim_interface=lambda dev, i: None, get_string=lambda dev, i: "EMU")
    saved = {k: sys.modules.get(k) for k in ("usb", "usb.core", "usb.util")}
    sys.modules.update({"usb": usb, "usb.core": usb.core, "usb.util": usb.util})
    argv, sys.argv = sys.argv, ["badge.py"] + list(args)
    out, exit_msg = io.StringIO(), None
    try:
        with contextlib.redirect_stdout(out):
            runpy.run_path(os.path.join(ROOT, "tools", "badge.py"), run_name="__main__")
    except SystemExit as x:
        exit_msg = x.code
    finally:
        sys.argv = argv
        for k, v in saved.items():
            if v is None:
                sys.modules.pop(k, None)
            else:
                sys.modules[k] = v
    return out.getvalue(), exit_msg


def power_on(path, **kw):
    """The factory test's first step at power-on, or nothing for 1.5 s;
    returns the emulator and whether the test started"""
    e = Emu(path, **kw)
    started = wait_for(lambda: logged(e, "factory LED"), 1.5) and wait_for(lambda: shows_still(e.wait_frames(), "LED"), 1)
    return e, started


def leave(e):
    """Hold A + B for 3 s"""
    e.buttons(BTN_A | BTN_B)
    time.sleep(3.4)
    e.buttons(0)
    return wait_for(lambda: logged(e, "factory left"), 1)


def usb_step(e):
    """The USB step, with the emulator's USB power (PC3) and charger (PB22)"""
    check(e.pins() == (False, True), "no USB power: PC3 low, and the charger off: CHRG (PB22) high")
    seen = frames(e, 2.0)  # USB out since before the step, and the battery charges
    check(any(0 < label(f, "USB", 2) <= 30 for f in seen), "no USB power: USB blinks")
    check(not any(chg(f) for f in seen), "no USB power: no CHG, though the battery would charge")
    check(not logged(e, "factory IR"), "no USB power: the step waits")

    # (the waits leave room for a firmware that wants CHRG steady for 500 ms)
    e.usb_power(True)
    check(e.pins() == (True, False), "USB power in: PC3 high, CHRG low")
    check(wait_for(lambda: chg(e.frame[1:]), 0.9), "USB power in: CHG")
    check(label(e.frame[1:], "USB", 2) == 255, "USB power in: USB stays lit")
    e.usb_power(False)  # out again within the second it needs
    check(wait_for(lambda: not chg(e.frame[1:]), 0.8), "USB power out: CHG goes")
    time.sleep(1.0)
    check(not logged(e, "factory IR"), "less than a second of USB power is not enough")

    e.usb_power(True)
    e.charging(False)
    check(wait_for(lambda: label(e.frame[1:], "USB", 2) == 255, 0.6) and not chg(e.frame[1:]),
          "USB power in, the battery full: USB, no CHG")
    e.no_battery()  # CHRG toggles every 100 ms
    high = []
    for _ in range(30):
        high.append(e.pins()[1])
        time.sleep(0.02)
    check(any(high) and not all(high), "no battery: CHRG toggles (high in %d of %d samples)" % (sum(high), len(high)))
    seen = frames(e, 0.6)
    check(not all(chg(f) for f in seen), "no battery: CHG does not stay on (%d of %d frames)" % (sum(map(chg, seen)), len(seen)))
    e.charging(False)  # a battery again, for the tests after this one
    check(wait_for(lambda: logged(e, "factory IR"), 2), "USB power for a second: IR")


def new_badge(path):
    """Flashed with the factory image: the test at power-on; A + B leaves it,
    and it stays; badge.py factory starts it again"""
    e, started = power_on(path)
    try:
        check(started, "the factory image: the test at power-on")
        check(e.usb(QUERY)[0] == MODE_APP, "it is an app")
        check(items(e) == ["FACTORY"], "the store holds only the test: %r" % items(e))
        check(brightness(e) == 255, "the test runs at full brightness")
        check(leave(e), "A + B held 3 s leaves it")
        check(wait_for(lambda: e.usb(QUERY)[0] == MODE_NAMETAG, 1), "then the name tag")
        check(brightness(e) == 32, "at a new badge's brightness: 32")
        check(items(e) == ["FACTORY"], "the test stays in the store")
        out, msg = badge_py(e, "factory")
        check(msg is None and "item 0" in out, "badge.py factory starts the stored test: %r" % (msg or out.strip()))
        check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2) and items(e) == ["FACTORY"], "LED, not stored twice")
        check(leave(e), "left again")
        time.sleep(1.5)  # the persistent record is written 1 s after the last change
    finally:
        e.stop()


def run(path):
    """The next power-on: the test again, all of it"""
    e, started = power_on(path)
    try:
        check(started, "left: the test again at the next power-on")
        e.set_name("ALICE")
        check(e.usb(QUERY)[0] == MODE_APP, "a name from the host: the test goes on")
        time.sleep(1.2)
        check(lit(e) >= 370, "LED: every LED on (%d lit)" % lit(e))
        e.buttons(BTN_A)  # A held: no menu, and LED goes on by itself
        time.sleep(1.2)
        e.buttons(0)
        check(not logged(e, "factory FAIL") and e.usb(QUERY)[0] == MODE_APP, "A does nothing in LED (and opens no menu)")
        check(wait_for(lambda: logged(e, "factory ROW"), 5), "LED goes on to ROW by itself")
        time.sleep(1.3)
        n = lit(e)
        check(n in (16, 24), "ROW: one line at a time (%d lit)" % n)
        check(wait_for(lambda: logged(e, "factory KEY"), 7), "ROW goes on to KEY by itself")
        time.sleep(1.2)
        for b in (BTN_C, BTN_A, BTN_D):
            e.press(b)
        check(not logged(e, "factory USB"), "three of four buttons: not yet")
        check(wait_for(lambda: logged(e, "factory FAIL KEY: missing: . B . ."), 22), "20 s without B: KEY fails, B missing")
        e.press(BTN_B)
        check(wait_for(lambda: shows_still(e.wait_frames(), "KEY"), 1), "B tries KEY again")
        time.sleep(1.2)
        e.usb_power(False)  # unplugged before the USB step, and the battery would charge
        e.charging(True)
        for b in (BTN_A, BTN_B, BTN_C, BTN_D):
            e.press(b)
        check(wait_for(lambda: logged(e, "factory USB"), 1), "all four: USB")
        time.sleep(1.1)  # the step's name
        usb_step(e)
        check(wait_for(lambda: logged(e, "factory PASS"), 4), "IR hears its own echo: PASS")
        check(b"T\x04TEST" in e.ir_sent, "it sent test packets: %r" % e.ir_sent[:1])
        check(wait_for(lambda: shows_still(e.wait_frames(), "OK"), 1), "OK")
        shutil.copyfile(path, path + ".ok")  # the flash as the badge is unplugged at the OK
        check(name_tag(e, 6), "then the name tag")
        check(brightness(e) == 32, "and the badge's own brightness again: 32")
        check(items(e) == [], "the test deleted itself: the store is empty")
        time.sleep(1.5)
    finally:
        e.stop()


def after_pass(path):
    """Nothing at power-on after the pass; badge.py factory stores it again
    and keeps the boot item; the power goes during the test"""
    e, started = power_on(path)
    try:
        check(not started, "passed: no test at power-on")
        check(name_tag(e, 3), "the name tag")
        check(e.store(TYPE_APP, "ripple", open(APP, "rb").read())[2] == 0, "an app stored: the boot item")
        check(e.usb(QUERY)[5] == 0, "boot item 0")
        out, msg = badge_py(e, "factory")
        check(msg is None and "stored as item 1" in out, "badge.py factory stores the test: %r" % (msg or out.strip()))
        check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "and starts it")
        r = e.usb(QUERY)
        check(r[4] == 1 and r[5] == 0, "the test runs (item %d), the boot item stays (%d)" % (r[4], r[5]))
        check(brightness(e) == 255, "at full brightness")
        time.sleep(1.5)  # the power goes during the test: the record is written 1 s after a change
    finally:
        e.stop()
    e = Emu(path)
    try:
        e.wait_frames()
        check(brightness(e) == 32, "power cut during the test, then on: brightness 32, not 255")
        check(items(e) == ["ripple"], "passed before: the test deleted at power-on, not run")
        check(wait_for(lambda: e.usb(QUERY)[0] == MODE_APP, 1), "the boot item runs")
        badge_py(e, "factory")
        check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "badge.py factory again")
        check(leave(e), "left")
        check(wait_for(lambda: e.usb(QUERY)[0] == MODE_APP and e.usb(QUERY)[4] == 0, 1), "back to the boot item")
        check(items(e) == ["ripple"], "on a badge that passed before, leaving deletes the test too")
    finally:
        e.stop()


def unplugged_at_ok(path):
    """Unplugged as soon as OK shows: the pass is kept, so no test at the
    next power-on"""
    e, started = power_on(path)
    try:
        check(not started, "unplugged at the OK: no test at the next power-on")
        check(items(e) == [], "and the test deleted from the store: %r" % items(e))
    finally:
        e.stop()


def firmware_update(path):
    """A record that never passed (from before the test, or erased), but no
    test in the store, as after a firmware-only update: nothing starts"""
    data = bytearray(open(path, "rb").read())
    store = data[factory_image.EMU_STORE:factory_image.EMU_STORE + factory_image.STORE_SIZE]
    factory_image.erase_record(store)
    data[factory_image.EMU_STORE:factory_image.EMU_STORE + factory_image.STORE_SIZE] = store
    open(path, "wb").write(data)
    e, started = power_on(path)
    check(not started, "a record that never passed, no test stored: nothing at power-on")
    e.stop()


def fresh(path):
    """--fresh: the record erased, the items kept, the test stored after them"""
    e, started = power_on(path, fresh=True)
    try:
        check(started, "fresh: the factory test at power-on")
        name = e.usb(NAME_GET)
        check(name[8:].rstrip(b"\0") == b"", "fresh: the record is erased, so the name too: %r" % name[8:].rstrip(b"\0"))
        check(items(e) == ["ripple", "FACTORY"], "fresh: the items stay, the test after them: %r" % items(e))
        check(leave(e), "left without a pass")
        time.sleep(1.5)
    finally:
        e.stop()
    e, started = power_on(path, fresh=True)
    check(started and items(e) == ["ripple", "FACTORY"], "fresh again: the test, stored once")
    e.stop()
    e, started = power_on(path)
    check(started, "and at the next power-on, as it has not passed")
    e.stop()


def unplugged(path):
    """USB packets while USB power is out: dropped, as on the badge"""
    e = Emu(path)
    try:
        e.wait_frames()
        e.usb_power(False)
        e.send("u %02x" % QUERY)
        time.sleep(0.5)
        check(e.replies.empty(), "no USB power: a USB packet gets no reply")
        check(logged(e, "USB is unplugged: packet dropped"), "no USB power: the emulator logs the dropped packet")
        e.usb_power(True)
        check(len(e.usb(QUERY)) > 0, "USB power in: the badge answers again")
    finally:
        e.stop()


def main():
    app = open(factory_image.APP, "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        # the emulator does not run the firmware's part: a stand-in for it
        image = factory_image.factory_image(factory_image.FW_MARKER + b"emulator", app)
        path = os.path.join(tmp, "factory.bin")
        open(path, "wb").write(image)
        check(len(image) == factory_image.IMAGE_SIZE == 62 * 1024 - 256,
              "the image: 62 KB less the last page, so minichlink writes nothing past the flash (%d bytes)" % len(image))
        e, started = power_on(os.path.join(tmp, "new.bin"))
        check(not started, "a new, empty flash file: no test")
        e.stop()
        copy = os.path.join(tmp, "copy.bin")
        open(copy, "wb").write(image)
        e, started = power_on(copy, fresh=True)
        check(started and items(e) == ["FACTORY"], "fresh on the factory image: the test, stored once")
        e.stop()
        new_badge(path)
        run(path)
        unplugged_at_ok(path + ".ok")
        after_pass(path)
        unplugged(path)
        flash = os.path.join(tmp, "items.bin")  # the emulator's own flash file, with an item
        e = Emu(flash)
        e.wait_frames()
        check(e.store(TYPE_APP, "ripple", open(APP, "rb").read())[2] == 0, "an item stored in a flash file")
        e.stop()
        firmware_update(flash)
        fresh(flash)
    print("%s  factory test: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


main()
