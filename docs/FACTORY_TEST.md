# Factory test

A freshly flashed badge starts with a short factory test, made for the
volunteers at the flashing table. It takes about 30 seconds per badge, ends
in pass or fail, and needs no terminal.

It comes back at every power-on until it has passed once. The persistent
record keeps that (`persist.c`), next to the name. A firmware update (USB
boot ROM or the web interface) keeps the record, so the test does not come
back. Erasing the whole flash brings it back.

To run it again on purpose, use `tools/badge.py factory` (USB command `0x52`).

Each step shows its name for a second first. The outline fills up as the
steps go by.

| Step | The badge shows | You do |
|---|---|---|
| **LED** | Every LED on: white, then the arms red, green, blue, over and over | All lit, right colours: tap **B**. Something dark or the wrong colour: tap **A** |
| **ROW** | One row lit, moving top to bottom, then one column, moving left to right, then the columns that only drive the outline and the arms | Always exactly one row, column or set of dots, never two, none missing: tap **B**. Otherwise tap **A** |
| **KEY** | `A` `B` `C` `D`, dimmed, where the buttons are | Press each button once; each lights up. It goes on by itself |
| **USB** | `USB`, blinking until it sees USB power; `CHG` while the battery charges | Keep it plugged in. It goes on after a second of USB power |
| **IR** | The packets heard (top) and sent (bottom) | Nothing. It passes when it hears its own test packets (the receiver sees the badge's own IR LED), or another badge's |
| **IR?** | After 10 s without hearing one: `IR?`, amber arms. It keeps listening | Hold a hand or a sheet of paper a few cm in front: it passes as soon as it hears a packet. **B** passes it anyway, **A** fails it |
| **OK** | Green arms, `OK` | Done. After 3 s the badge goes on to its boot item or name tag |

A failed step blinks red arms and shows the step's name: put the badge on
the repair pile. Tap **B** to try that step once more. KEY fails after
20 s, and USB after 10 s.

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
> 2. **LED**: every LED lit? Arms go red, green, blue? Yes: **B**. No: **A**.
> 3. **ROW**: one line moving at a time, never two? Yes: **B**. No: **A**.
> 4. **KEY**: press A, B, C and D once each.
> 5. **USB** and **IR** go by themselves. Keep it on USB.
>    Amber **IR?**: hold your hand a few cm in front. Still **IR?**: **A**.
> 6. Green **OK**: good badge. Red: repair pile.
