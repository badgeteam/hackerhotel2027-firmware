#!/usr/bin/env python3
"""The charging screen of the NAMETAG app (apps/nametag) and apps'
charging() on the emulator (tools/emu/headless.py).

When the battery starts to charge while the app runs, also at power-on
with the app as the boot item, a battery outline shows on the centre grid
and fills up row by row, over and over. A press of any button hides it,
and that press does nothing else (B and D leave the style alone, the
release of C opens no panel). It comes back only when charging stops and
starts again, and goes when charging stops. The app that starts while the
battery already charges shows no battery. CHRG must keep its level for
500 ms (power.c): a line that toggles faster never shows it, and neither
does the charger without a battery (CHRG toggling every 100 ms), at
power-on or later. Unplugging USB stops the charger and ends it. Under
it the app goes on: a banana shot still counts. The menu, a host command
or the factory test over it pauses or stops the app; the firmware itself
shows no battery (not on its name tag, not over other apps). An app's
charging() follows the same debounced state.

The emulator's USB power and charger come from headless.py: usb_power(),
charging(), no_battery() and pins().

  python3 tools/charge_test/emu_charge_test.py <nametag.hhapp> <charging.hhapp> <factory.hhapp>

charging.hhapp is tools/charge_test/charging_app.c built as an app (run.sh
builds it): it prints "CHARGING 1" or "CHARGING 0" when charging() changes.
"""
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu"))
from headless import *  # noqa: E402,F403

CLEAR, ITEM_RUN, STOP = 0x05, 0x25, 0x26
TYPE_APP, MODE_CUSTOM, MODE_APP, MODE_NAMETAG = 2, 1, 3, 4
NAME = "BOB"
failures = 0

# The battery as the app draws it: the outline (x 4 .. 12, y 3 .. 14, and
# the tip x 7 .. 9 on y 2) and what fills (x 6 .. 10, y 5 .. 12)
OUTLINE = [GRID[y][x] for y in range(3, 15) for x in range(4, 13) if y in (3, 14) or x in (4, 12)]
OUTLINE += [GRID[2][x] for x in range(7, 10)]
FILL = [GRID[y][x] for y in range(5, 13) for x in range(6, 11)]
INSIDE = set(GRID[y][x] for y in range(2, 15) for x in range(4, 13))
OUTSIDE = [s for s in range(16 * COLS) if s not in INSIDE]
assert None not in OUTLINE + FILL and len(FILL) == 40
# The LED that charging_app.c lights while charging() is true
APP_LED = GRID[8][8]


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


def name_shows(e, seconds=1.0):
    """The name tag (the app's style 0, or the firmware's) shows NAME"""
    return wait_for(lambda: shows_still(e.wait_frames(), NAME), seconds)


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


def mode(e):
    return e.usb(QUERY)[0]


# How long a test holds charging off so that the firmware counts it. The
# debounce counts at most 20 ms a pass (power.c), so 500 ms takes longer in
# real time when the emulator runs slow: 1.2 s leaves room for 48 ms passes.
OFF_HOLD = 1.2


def charge_cycle(e):
    """Charging stops, long enough to count, and starts again"""
    e.charging(False)
    time.sleep(OFF_HOLD)
    e.charging(True)


def install(path, nametag):
    """A badge named NAME with the NAMETAG app as its boot item"""
    e = Emu(path)
    try:
        e.set_name(NAME)
        r = e.store(TYPE_APP, "NAMETAG", nametag)
        check(r[2] == 0 and r[0] == MODE_APP, "the NAMETAG app is stored and runs")
        time.sleep(1.5)  # the name is written (persist.c)
    finally:
        e.stop()


def power_on(path):
    """Power-on with the NAMETAG app as the boot item: without a battery
    (CHRG toggles from the start) nothing shows; with a battery that
    charges the battery shows, after the debounce"""
    e = Emu(path)
    try:
        e.no_battery()
        check(chrg_toggles(e), "power-on without a battery: CHRG toggles")
        seen = frames(e, 6.0)
        check(seen and not any(battery(f) for f in seen), "power-on without a battery, 6 s: no battery screen")
        check(mode(e) == MODE_APP and name_shows(e), "the app shows the name")
    finally:
        e.stop()
    e = Emu(path)
    try:
        e.charging(True)
        check(not any(battery(f) for f in frames(e, 0.35)), "power-on, the battery charges: not at once (500 ms)")
        check(shows_battery(e, 2.0), "then the battery on the grid")
        check(mode(e) == MODE_APP, "the app runs under it")
    finally:
        e.stop()


def run(e, charging_app):
    check(mode(e) == MODE_APP and name_shows(e, 3.0), "the NAMETAG app runs, the name shows")

    # Charging starts while the app runs: the battery shows after the debounce
    e.charging(True)
    check(not any(battery(f) for f in frames(e, 0.35)), "not at once: CHRG must hold for 500 ms")
    check(shows_battery(e, 2.0), "charging starts: a battery on the grid")
    seen = frames(e, 4.0)
    lit = {sum(1 for s in FILL if f[s]) for f in seen if battery(f)}
    check(all(battery(f) for f in seen), "it stays for 4 s")
    check(0 in lit and 40 in lit and len(lit) >= 6, "it fills row by row, from empty to full, over and over (%s)" % sorted(lit))
    check(mode(e) == MODE_APP, "the app runs under it")

    # A press hides it, and does nothing else (the style is checked after a
    # power cycle, in main())
    e.press(BTN_C)
    check(wait_for(lambda: not battery(e.frame[1:]), 0.5), "C hides it")
    check(name_shows(e), "the name shows again, no panel")
    check(no_battery(e, 1.5), "it does not come back while charging goes on")
    charge_cycle(e)
    check(shows_battery(e), "charging stops and starts again: the battery is back")
    e.press(BTN_B)
    check(name_shows(e), "B hides it, the name shows")
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.press(BTN_D)
    check(name_shows(e), "D hides it, the name shows")

    # Charging stops: it goes by itself
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.charging(False)
    check(name_shows(e, 1.5), "charging stops: it goes, the name shows")

    # CHRG toggling faster than 500 ms: nothing shows
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
    check(name_shows(e), "the name shows on")
    e.charging(False)  # a battery again
    time.sleep(OFF_HOLD)

    # Unplugged: the charger stops, and it goes; plugged in again, it is back
    e.charging(True)
    check(shows_battery(e), "again")
    e.usb_power(False)
    check(e.pins() == (False, True), "USB unplugged: PC3 low, CHRG high (the charger off)")
    check(name_shows(e, 1.5), "USB unplugged: it goes, the name shows")
    check(no_battery(e, 1.0), "and stays gone while unplugged")
    e.usb_power(True)
    check(shows_battery(e), "plugged in again, the battery charges: it is back")

    # Under it the app goes on: a shot counts, and shows once it is hidden
    e.shot(0, SHOT_SHOOT)
    time.sleep(0.2)
    check(battery(e.frame[1:]), "a shot: the battery stays")
    e.press(BTN_C)
    check(wait_for(lambda: shows_still(e.wait_frames(), "1"), 1.5), "C hides it: the hit count 1 shows")
    check(name_shows(e, 3.0), "then the name")

    # Hold A over it: the app gets the press, which hides it; the menu
    # opens, and closed (hold A) the app goes on without it
    charge_cycle(e)
    check(shows_battery(e), "again")
    e.buttons(BTN_A)
    time.sleep(1.3)  # the menu opens, 1 s after A went down in an app
    e.buttons(0)
    time.sleep(0.2)
    check(not battery(e.frame[1:]), "hold A: the menu, not the battery")
    e.buttons(BTN_A)
    time.sleep(1.0)  # held: the menu closes
    e.buttons(0)
    check(name_shows(e, 1.5) and mode(e) == MODE_APP, "the menu closed: the app goes on, the name")
    check(no_battery(e, 1.0), "and no battery")

    # A host command stops the app; the firmware shows no battery
    charge_cycle(e)
    check(shows_battery(e), "again")
    check(e.usb(CLEAR)[0] == MODE_CUSTOM, "CLEAR: custom mode")
    check(no_battery(e, 1.0), "the host's command ends it")
    check(e.usb(STOP)[0] == MODE_NAMETAG, "STOP: the firmware's name tag")
    check(no_battery(e, 1.0), "and no battery")
    charge_cycle(e)
    check(no_battery(e, 1.5), "charging starts on the firmware's name tag: no battery")

    # The factory test (an app); after it NAMETAG (item 0) starts again while
    # the battery charges: no battery
    r = e.store(TYPE_APP, "FACTORY", factory_app)
    check(r[2] == 0 and wait_for(lambda: shows_still(e.wait_frames(), "LED"), 2), "the factory test app: LED")
    e.buttons(BTN_A | BTN_B)  # A + B held 3 s leaves it
    time.sleep(3.4)
    e.buttons(0)
    time.sleep(0.5)
    e.usb(ITEM_RUN, 0, 1)
    check(wait_for(lambda: mode(e) == MODE_APP, 3) and name_shows(e, 3.0), "left, NAMETAG run: the NAMETAG app again")
    check(no_battery(e, 1.5), "started while the battery charges: no battery")
    e.charging(False)
    time.sleep(OFF_HOLD)

    # Not over another app, and the app's charging() follows the debounced state
    n = len(e.log)
    r = e.store(TYPE_APP, "CHARGING", charging_app)
    check(r[2] == 0 and r[0] == MODE_APP, "the charging app runs")
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 0"], 1.0), "charging(): false")
    n = len(e.log)
    e.charging(True)
    check(not wait_for(lambda: app_says(e, n), 0.35), "charging starts: not at once in the app either")
    check(wait_for(lambda: app_says(e, n) == ["CHARGING 1"], 1.5), "then charging(): true")
    check(no_battery(e, 1.5), "charging starts during the app: no battery")
    check(e.wait_frames()[APP_LED] == 255, "the app draws on (its LED for charging)")
    check(mode(e) == MODE_APP, "the app runs on")
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


def main():
    nametag = open(sys.argv[1], "rb").read()
    charging_app = open(sys.argv[2], "rb").read()
    global factory_app
    factory_app = open(sys.argv[3], "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "flash.bin")
        install(path, nametag)
        power_on(path)
        e = Emu(path)
        try:
            run(e, charging_app)
        finally:
            e.stop()
        # The presses that hid the battery left the style alone: stored
        # again, the app starts with the saved style, still 0
        e = Emu(path)
        try:
            n = len(e.log)
            r = e.store(TYPE_APP, "NAMETAG", nametag)
            check(r[2] == 0 and wait_for(lambda: any("nametag: style 0" in line for line in e.log[n:]), 3),
                  "the NAMETAG app again: style 0, as before")
        finally:
            e.stop()
    print("%s  charging screen: %d failures" % ("FAIL" if failures else "ok  ", failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
