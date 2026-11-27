# Factory test

A badge flashed with the factory image starts with a short factory test,
made for the volunteers at the flashing table. It takes about 30 seconds
per badge, ends in pass or fail, and needs no terminal.

The test is an app, `apps/factory`, not a part of the firmware. The
factory image holds the firmware and a store with only this app in it,
under the name `FACTORY`. The firmware starts that app at every power-on,
before anything else, while it is in the store and the test has not
passed. When the test passes, the persistent record keeps that
(`persist.c`), and the firmware deletes the app. The store is then empty,
with all its room for the recommended apps. If the test is left (A + B),
the app stays, and the test runs again at the next power-on.

A firmware update (USB boot ROM, the web interface, or `ledmatrix.bin`
with minichlink or wlink) writes only the firmware's own pages. It starts
no test: the test runs only when the app is in the store.

For the same reason, install the recommended apps only after the OK.
"Erase badge and install recommended apps" in the web interface,
`tools/badge.py recommended` and `tools/badge.py erase` erase the store,
and the test with it: a badge that has not passed is then not tested
again. To get the test back, run `tools/badge.py factory` or flash
`factory.bin` again.

## The factory image

```sh
make -C ch32fun/minichlink                         # once: the flash tool
make MINICHLINK=ch32fun/minichlink factory-image   # writes factory.bin
```

`factory.bin` fills the 62 KB of flash but its last page: the firmware,
then the store with the factory test app, then an empty persistent record
(`tools/factory_image.py` puts it together). The last page is erased
anyway; without it, minichlink's last 56-byte chunk over the boot ROM
stays inside the flash. Flashing it erases the stored items, the name and
the settings. Flash it as `README.md` says for `ledmatrix.bin`, but with
`factory.bin`:

- Over USB, with the boot ROM: `ch32fun/minichlink/minichlink -w
  factory.bin flash -b` (to confirm on prototype 1). The web interface's
  firmware update writes only the firmware, so it cannot flash
  `factory.bin`.
- With a WCH-LinkE: `wlink --speed low --chip CH643 flash --address
  0x08000000 factory.bin`.

The badge restarts and the test starts by itself.

To run the test again on a badge, use `tools/badge.py factory`. It
stores the app (`apps/build/factory.hhapp`, from `make -C apps`) unless it
is stored already, and starts it. A badge with an older firmware (app API
older than 9) needs a firmware update first. On a badge that has passed
the test before, the app is deleted when the test ends, also when it is
left, and at the next power-on. The badge's boot item stays.

The test runs at full brightness, so a weak LED or a wrong colour shows.
After it, the badge has its own brightness again (12.5 % on a new badge).
The firmware does not keep the full brightness, also not when the power
goes during the test.

Each step shows its name for a second first. The outline fills up as the
steps go by. Only KEY needs a hand: the rest runs by itself, and the
volunteer watches.

| Step | The badge shows | Look for |
|---|---|---|
| **LED** | Every LED on: white, then the arms red, green, blue (4.5 s) | All lit, the right colours |
| **ROW** | One row lit, moving top to bottom, then one column, moving left to right, then the columns that only drive the outline and the arms (5 s) | Always exactly one row, column or set of dots, never two, none missing |
| **KEY** | `A` `B` `C` `D`, dimmed, where the buttons are | Press each button once; each lights up |
| **USB** | `USB`, blinking until it sees USB power; `CHG` while the battery charges | Keep it plugged in. It goes on after a second of USB power |
| **IR** | The packets heard (top) and sent (bottom) | Nothing: it passes when it hears its own test packets (the receiver sees the badge's own IR LED), or another badge's |
| **IR?** | After 10 s without hearing one: `IR?`, amber arms. It keeps listening | Hold a hand or a sheet of paper a few cm in front. After 30 s it fails |
| **OK** | Green arms, `OK` | Done. After 3 s the badge goes on to its boot item or name tag |

Something wrong in LED or ROW (a dark LED, a wrong colour, two lines at
once): put the badge on the repair pile, whatever it says at the end.

A failed step blinks red arms and shows the step's name: the repair pile.
Tap **B** to try that step once more. KEY fails after 20 s, USB after
10 s, IR after 30 s.

**Fully automatic:** build with `make factory-image FACTORY_KEYS=0` and
the KEY step is left out: the test needs no hand at all.

Holding **A + B for 3 s** leaves the test without passing it. The app
stays in the store, so the test runs again at the next power-on. The menu
(hold A) does not open during the test. C + D for 3 s still goes to the
boot ROM.

The badge logs each step over SWIO (`minichlink -T`), for example
`factory LED`, `factory FAIL KEY: missing: . . C D`, `factory PASS`.

## Hardware

- **USB power:** PC3 sees VBUS through R37 to GND, so it is high while USB
  powers the badge.
- **Charging:** the XT4054 charger's CHRG output (PB22, open-drain) is low
  while the battery charges. The firmware takes a change only after it
  holds for 500 ms (`power.c`), so `CHG` comes half a second late. With no
  battery the charger toggles CHRG faster than that, so `CHG` never shows
  (it does not flicker).
- **IR:** the receiver (S1) hears the badge's own IR LED (D315). During the
  IR step the driver decodes its own transmission (the app API's
  `ir_loopback`, `infrared_set_loopback`).

## The app API it uses

The app needs app API 9 (`apps/hhapp.h`, `docs/APP_API.md`): `usb_power`
for the USB step, `ir_loopback` for the IR step, and `factory_passed`,
which records the pass. The firmware deletes the app when it exits after
that.

## Card for the flashing table

> 1. Flash the badge with `factory.bin`. Keep it on USB. It starts the
>    test by itself.
> 2. **LED**: watch: every LED lit? Arms go red, green, blue?
> 3. **ROW**: watch: one line moving at a time, never two?
> 4. **KEY**: press A, B, C and D once each.
> 5. **USB** and **IR** go by themselves. Keep it on USB.
>    Amber **IR?**: hold your hand a few cm in front.
> 6. Green **OK** and nothing odd in 2 or 3: good badge. Otherwise: repair pile.
> 7. Recommended apps: only after the green **OK**. Erasing the badge
>    before that takes the test away.
