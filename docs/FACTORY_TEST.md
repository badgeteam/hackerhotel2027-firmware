# Factory test

A freshly flashed badge starts with a short factory test, made for the
volunteers at the flashing table. It takes about 30 seconds per badge, ends
in pass or fail, and needs no terminal.

It comes back at every power-on until it has passed once. The persistent
record keeps that (`persist.c`), next to the name. A firmware update (USB
boot ROM or the web interface) keeps the record, so the test does not come
back. Erasing the whole flash brings it back.

To run it again on purpose, use `tools/badge.py factory` (USB command `0x52`).

The test runs at full brightness, so a weak LED or a wrong colour shows.
After it, the badge has its own brightness again (12.5 % on a new badge).

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

**Fully automatic:** build with `make FACTORY_KEYS=0` (or
`FACTORY_KEYS=0 make`) and the KEY step is left out: the test needs no hand
at all.

Holding **A + B for 3 s** leaves the test without passing it. It then runs
again at the next power-on.

The badge logs each step over SWIO (`minichlink -T`), for example
`factory LED`, `factory FAIL KEY: missing: . . C D`, `factory PASS`.

## Hardware

- **USB power:** PC3 sees VBUS through R37 to GND, so it is high while USB
  powers the badge.
- **Charging:** the XT4054 charger's CHRG output (PB22, open-drain) is low
  while the battery charges.
- **IR:** the receiver (S1) hears the badge's own IR LED (D315). During the
  IR step the driver decodes its own transmission (`infrared_set_loopback`).

## Card for the flashing table

> 1. Flash the badge. Keep it on USB. It starts the test by itself.
> 2. **LED**: watch: every LED lit? Arms go red, green, blue?
> 3. **ROW**: watch: one line moving at a time, never two?
> 4. **KEY**: press A, B, C and D once each.
> 5. **USB** and **IR** go by themselves. Keep it on USB.
>    Amber **IR?**: hold your hand a few cm in front.
> 6. Green **OK** and nothing odd in 2 or 3: good badge. Otherwise: repair pile.
