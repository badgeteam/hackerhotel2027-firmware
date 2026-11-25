#!/usr/bin/env python3
"""The charging screen (charge.c) on the emulator (tools/emu/headless.py).

When the battery starts to charge, also at power-on, a battery outline
shows on the centre grid and fills up row by row, over and over. A button
hides it, and that press does nothing else (B and D leave the brightness
alone). It comes back only when charging stops and starts again, and goes
when charging stops. It never shows over an app, the menu or a greeting,
and a host command that changes what runs ends it. It never covers what
the host drew (custom mode). Hiding it puts back what showed before; a
shot or the menu while it shows, or the factory test over it, does not
bring it back. CHRG must keep its level for 500 ms (power.c): a line that
toggles faster never shows it, and neither does the charger without a
battery (CHRG toggling every 100 ms), at power-on or later. Unplugging USB
stops the charger and ends it. An app's charging() follows the same
debounced state, and the screen never shows over the app.

The emulator's USB power and charger come from headless.py: usb_power(),
charging(), no_battery() and pins().

  python3 tools/charge_test/emu_charge_test.py <charging.hhapp>

charging.hhapp is tools/charge_test/charging_app.c built as an app (run.sh
builds it): it prints "CHARGING 1" or "CHARGING 0" when charging() changes.
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

SET_PIXEL, CLEAR, STORE_ERASE, STOP, FACTORY = 0x01, 0x05, 0x20, 0x26, 0x52
TYPE_APP, MODE_CUSTOM, MODE_PATTERN, MODE_APP, MODE_NAMETAG = 2, 1, 2, 3, 4
NAME = "BOB"
failures = 0

# The battery as charge.c draws it: the outline (x 4 .. 12, y 3 .. 14, and
# the tip x 7 .. 9 on y 2) and what fills (x 6 .. 10, y 5 .. 12)
OUTLINE = [GRID[y][x] for y in range(3, 15) for x in range(4, 13) if y in (3, 14) or x in (4, 12)]
OUTLINE += [GRID[2][x] for x in range(7, 10)]
FILL = [GRID[y][x] for y in range(5, 13) for x in range(6, 11)]
INSIDE = set(GRID[y][x] for y in range(2, 15) for x in range(4, 13))
OUTSIDE = [s for s in range(16 * COLS) if s not in INSIDE]
assert None not in OUTLINE + FILL and len(FILL) == 40
# An LED on the battery's square that the battery leaves dark
DOT = GRID[2][4]
assert DOT is not None and DOT in INSIDE and DOT not in OUTLINE + FILL
# The LED that charging_app.c lights while charging() is true
APP_LED = GRID[8][8]
# Grid LEDs above and below the text: dark on the name tag, lit by a shot's ring
OFF_TEXT = [s for y, row in enumerate(GRID) if y < TEXT_Y or y >= TEXT_Y + 8 for s in row if s is not None]


def check(cond, what):
    global failures
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures += 1


def battery(frame):
    """The charging screen: the whole outline lit, nothing else off the battery"""
    return all(frame[s] for s in OUTLINE) and not any(frame[s] for s in OUTSIDE)


def frames(emu, seconds):
    """The frames the emulator sends for a while"""
    seen, last, end = [], None, time.time() + seconds
    while time.time() < end:
        f = emu.frame
        if f is not None and f is not last:
            seen.append(f[1:])
            last = f
        time.sleep(0.005)
    return seen


def wait_for(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


def shows_battery(e, seconds=1.5):
    return wait_for(lambda: e.frame is not None and battery(e.frame[1:]), seconds)


def no_battery(e, seconds=1.5):
    """No frame with the battery for a while (after a moment: the
    emulator's last frame may still be from before)"""
    time.sleep(0.1)
    return not any(battery(f) for f in frames(e, seconds))


def brightness(e):
    return e.usb(QUERY)[1]


def hello(e, name):
    """A SAY HI from another badge (greet.h): type 'H', the name"""
    e.send("p " + bytes([ord("H"), len(name)]).hex() + name.encode().hex())


def still_animation(slot):
    """An animation of one frame, at 1 frame/s, with only LED slot lit,
    compressed by the web interface's encoder (webui/compression.js)"""
    js = (
        "const fs = require('fs'), path = require('path'), vm = require('vm'), c = {}; vm.createContext(c);"
        "for (const f of ['layout_data.js', 'compression.js'])"
        "  vm.runInContext(fs.readFileSync(path.join(process.argv[1], f), 'utf8'), c);"
        "const frame = new Array(384).fill(0); frame[+process.argv[2]] = 255;"
        "const bytes = vm.runInContext('compressPattern', c)({ fps: 1, frames: [frame] }, { quantBits: 1 });"
        "process.stdout.write(Buffer.from(bytes).toString('hex'));")
    return bytes.fromhex(subprocess.check_output(["node", "-e", js, os.path.join(ROOT, "webui"), str(slot)]).decode())


def app_says(e, since):
    """What the charging app printed after log line `since`"""
    return [line for line in e.log[since:] if line.startswith("CHARGING")]


def chrg_toggles(e):
    """CHRG (PB22) high in some samples and low in others, over 0.6 s"""
    high = []
    for _ in range(30):
        high.append(e.pins()[1])
        time.sleep(0.02)
    return any(high) and not all(high)


# How long a test holds charging off so that the firmware counts it. The
# debounce counts at most 20 ms a pass (power.c), so 500 ms takes longer in
# real time when the emulator runs slow: 1.2 s leaves room for 48 ms passes.
OFF_HOLD = 1.2


def charge_cycle(e):
    """Charging stops, long enough to count, and starts again"""
    e.charging(False)
    time.sleep(OFF_HOLD)
    e.charging(True)


def run(e, app):
    # Charging starts just after power-on (the firmware starts with charging
    # off, so this is the same as at reset; power_on_without_battery() runs
    # from reset): the battery shows after the debounce
    e.charging(True)
    check(not any(battery(f) for f in frames(e, 0.35)), "not at once: CHRG must hold for 500 ms")
    check(shows_battery(e, 2.0), "charging starts: a battery on the grid")
    seen = frames(e, 4.0)
    lit = {sum(1 for s in FILL if f[s]) for f in seen if battery(f)}
    check(all(battery(f) for f in seen), "it stays for 4 s")
    check(0 in lit and 40 in lit and len(lit) >= 6, "it fills row by row, from empty to full, over and over (%s)" % sorted(lit))
    check(e.usb(QUERY)[0] == MODE_NAMETAG, "the name tag waits under it")

    # A button hides it; the press does nothing else
    e.press(BTN_C)
    check(wait_for(lambda: not battery(e.frame[1:]), 0.5), "C hides it")
    e.set_name(NAME)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0), "the name tag shows again")
    check(no_battery(e, 1.5), "it does not come back while charging goes on")

    e.press(BTN_D)  # outside the charging screen D steps the brightness down
    level = brightness(e)
    check(level < 255, "D outside the charging screen: brightness %d" % level)
    charge_cycle(e)
    check(shows_battery(e), "charging stops and starts again: the battery is back")
    e.press(BTN_B)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0), "B hides it, the name tag shows")
    check(brightness(e) == level, "and the brightness is still %d" % level)
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.press(BTN_D)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0) and brightness(e) == level, "D hides it, brightness unchanged")

    # Charging stops: it goes by itself
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.charging(False)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.5), "charging stops: it goes, the name tag shows")

    # CHRG toggling faster than 500 ms (no battery in): nothing shows
    seen = []
    for i in range(12):
        e.charging(i % 2 == 0)
        seen += frames(e, 0.3)
    e.charging(False)
    check(not any(battery(f) for f in seen), "CHRG toggling every 300 ms never shows it")

    # No battery: the charger toggles CHRG every 100 ms; nothing shows, however long
    e.no_battery()
    check(chrg_toggles(e), "no battery: CHRG toggles")
    seen = frames(e, 6.0)
    check(seen and not any(battery(f) for f in seen), "no battery for 6 s: it never shows")
    check(shows_still(e.wait_frames(), NAME), "the name tag shows on")
    e.charging(False)  # a battery again
    time.sleep(OFF_HOLD)

    # Unplugged: the charger stops, and it goes; plugged in again, it is back
    e.charging(True)
    check(shows_battery(e), "again")
    e.usb_power(False)
    check(e.pins() == (False, True), "USB unplugged: PC3 low, CHRG high (the charger off)")
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.5), "USB unplugged: it goes, the name tag shows")
    check(no_battery(e, 1.0), "and stays gone while unplugged")
    e.usb_power(True)
    check(shows_battery(e), "plugged in again, the battery charges: it is back")
    e.press(BTN_C)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0), "C hides it")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # A host command that changes what runs ends it
    e.charging(True)
    check(shows_battery(e), "again")
    check(e.usb(CLEAR)[0] == MODE_CUSTOM, "CLEAR: custom mode")
    check(no_battery(e, 1.0), "the host's command ends it")
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the name tag")
    check(no_battery(e, 1.0), "and it does not come back")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Not over an app, and the app's charging() follows the debounced state
    n = len(e.log)
    r = e.store(TYPE_APP, "CHARGING", app)
    check(r[2] == 0 and r[0] == MODE_APP, "the charging app runs")
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 0"], 1.0), "charging(): false")
    n = len(e.log)
    e.charging(True)
    check(not wait_for(lambda: app_says(e, n), 0.35), "charging starts: not at once in the app either")
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 1"], 1.5), "then charging(): true")
    check(no_battery(e, 1.5), "charging starts during the app: no battery")
    check(e.wait_frames()[APP_LED] == 255, "the app draws on (its LED for charging)")
    check(e.usb(QUERY)[0] == MODE_APP, "the app runs on")
    n = len(e.log)
    e.charging(False)
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 0"], 1.5), "charging stops: charging() false")
    n = len(e.log)
    e.no_battery()
    seen = frames(e, 4.0)
    check(app_says(e, n) == [], "no battery for 4 s: charging() stays false")
    check(not any(battery(f) for f in seen), "and no battery screen")
    e.charging(True)
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 1"], 1.5), "a battery that charges: charging() true")
    n = len(e.log)
    e.no_battery()
    time.sleep(3.0)
    check(app_says(e, n) == [], "no battery after charging: charging() keeps the last steady state")
    e.charging(True)
    time.sleep(0.2)
    n = len(e.log)
    e.usb_power(False)
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 0"], 1.5), "USB unplugged: charging() false")
    n = len(e.log)
    e.usb_power(True)
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 1"], 1.5), "plugged in again: charging() true")
    check(no_battery(e, 1.0), "and no battery screen over the app")
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the name tag")
    check(no_battery(e, 1.0), "still none after the app")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Not over the menu
    e.buttons(BTN_A)
    time.sleep(1.0)  # the menu opens
    e.charging(True)
    check(no_battery(e, 1.2), "charging starts in the menu: no battery")
    e.buttons(0)
    time.sleep(0.15)
    e.press(BTN_A)  # NAME TAG
    check(e.usb(QUERY)[0] == MODE_NAMETAG, "NAME TAG from the menu")
    check(no_battery(e, 1.0), "still none after the menu")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Not over a greeting
    hello(e, "ANN")
    check(wait_for(lambda: shows_still(e.wait_frames(), "HI ANN"), 1.5), "a HELLO: HI ANN")
    e.charging(True)
    check(no_battery(e, 1.2), "charging starts during the greeting: no battery")
    e.press(BTN_C)  # ends the greeting
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0), "C ends the greeting: the name tag")
    check(no_battery(e, 1.0), "and no battery after it")

    # A greeting over the charging screen takes over
    charge_cycle(e)
    check(shows_battery(e), "again")
    hello(e, "ANN")
    check(wait_for(lambda: shows_still(e.wait_frames(), "HI ANN"), 1.5), "a HELLO over it: HI ANN")
    e.press(BTN_C)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 1.0), "C ends the greeting: the name tag, not the battery")
    check(no_battery(e, 1.0), "the battery stays gone")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Not over what the host drew (custom mode): the web interface's live
    # drawing sends single pixels after that, and must find them as it left them
    e.usb(CLEAR)
    check(e.usb(SET_PIXEL, DOT // COLS, DOT % COLS, 200)[0] == MODE_CUSTOM, "the host draws one LED: custom mode")
    e.charging(True)
    check(no_battery(e, 1.2), "charging starts over it: no battery")
    check(e.wait_frames()[DOT] == 200 and e.usb(QUERY)[0] == MODE_CUSTOM, "the host's LED is still on, custom mode")
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the name tag")
    check(no_battery(e, 1.0), "and no battery after it")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Over an animation that draws only once a second: hiding the battery
    # puts back what showed before at once
    r = e.store(TYPE_ANIMATION, "DOT", still_animation(DOT))
    check(r[2] == 0 and r[0] == MODE_PATTERN, "a still animation plays")
    check(wait_for(lambda: e.frame is not None and e.frame[1 + DOT] > 0, 2.0), "its one LED is on")
    e.charging(True)
    check(shows_battery(e, 2.0), "charging starts: the battery over the animation")
    e.press(BTN_C)
    f = e.frame[1:]
    check(not battery(f) and f[DOT] > 0, "C hides it: the animation's LED is back at once (%d)" % f[DOT])
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the name tag")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # A held from before the battery comes: the menu opens over it, and the
    # battery does not come back after the menu
    e.charging(True)
    time.sleep(0.2)
    e.buttons(BTN_A)
    check(shows_battery(e, 0.6), "A held as charging starts: the battery")
    time.sleep(1.0)  # the menu opens, 700 ms after A went down
    e.buttons(0)
    time.sleep(0.15)
    e.press(BTN_A)  # NAME TAG
    check(e.usb(QUERY)[0] == MODE_NAMETAG, "NAME TAG from the menu")
    check(no_battery(e, 1.0), "no battery after the menu")

    # A shot while the battery shows: the name tag does not show it later
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.shot(0, SHOT_SHOOT)
    time.sleep(0.2)
    e.press(BTN_C)
    time.sleep(0.1)
    seen = frames(e, 1.2)
    check(seen and not any(any(f[s] for s in OFF_TEXT) for f in seen), "a shot during it: no ring after C")

    # The factory test over it takes over; after it no battery (without a
    # boot item, which would start after the test and end it anyway)
    check(e.usb(STORE_ERASE)[2] == 0 and e.usb(STOP)[0] == MODE_NAMETAG, "the store erased: no boot item")
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.usb(FACTORY)
    check(wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "USB 0x52: the factory test, LED")
    e.buttons(BTN_A | BTN_B)  # A + B held 3 s leaves it
    time.sleep(3.4)
    e.buttons(0)
    check(wait_for(lambda: shows_still(e.wait_frames(), NAME), 3), "left: the name tag")
    check(no_battery(e, 1.0), "and no battery")
    e.charging(False)


def power_on_without_battery(path):
    """Power-on with USB in and no battery: CHRG toggles from the start"""
    e = Emu(path)
    try:
        e.no_battery()
        check(chrg_toggles(e), "power-on without a battery: CHRG toggles")
        seen = frames(e, 6.0)
        check(seen and not any(battery(f) for f in seen), "power-on without a battery, 6 s: no battery screen")
    finally:
        e.stop()


def main():
    app = open(sys.argv[1], "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        power_on_without_battery(os.path.join(tmp, "flash0.bin"))
        e = Emu(os.path.join(tmp, "flash.bin"))
        try:
            run(e, app)
        finally:
            e.stop()
    print("%s  charging screen: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
