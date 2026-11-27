# Bring-up: a newly assembled board

Use this checklist on a board that comes back from assembly. Do the steps
in this order. Each step checks a part that a later step needs, so a
fault shows up in the step that can explain it.

Each step tells you what to do, what you should see, and what to check
if you do not see it.

The part names, nets and values come from the KiCad files of
badgeteam/hackerhotel2027-hardware at f6015de, "Prototype 1
(production)": the same commit as `webui/board_data.js`. Values marked
**(to confirm on prototype 1)** come from the parts' data or from the
old firmware's notes. Nobody has measured them on this board with this
firmware yet. When you measure one, write the value here.

The hardware changed after f6015de: 8b5bb00 has a different battery
connector for J2, and d4e221e moves the battery polarity mark. Both
boards say "Prototype 1" on the silkscreen. A board from f6015de has a
single `+` mark near J2. A later board has a `-` and `+` mark: on that
board, examine J2 and its polarity yourself before you trust the J2 text
below.

The old firmware had its own bring-up document ([BRINGUP.md][old]). This
checklist replaces it. Its console commands (`level`, `test`) do not
exist in this firmware.

## What you need

- A lab supply with a current limit, 0 to 5 V.
- A multimeter. A scope is useful, but not necessary.
- A USB-C cable and a Linux PC with the udev rule from `README.md`,
  "Flash over USB".
- A USB-C power meter, to put between the PC and the badge (steps 4, 5
  and 8).
- `factory.bin` (`make MINICHLINK=ch32fun/minichlink factory-image`): the
  firmware and the factory test app, the 62 KB of flash but its last page
  (`docs/FACTORY_TEST.md`). Also minichlink (`make -C ch32fun/minichlink`)
  and pyusb for `tools/badge.py` (see `CONTRIBUTING.md`, "Build").
- For SWD: a WCH-LinkE with [wlink](https://github.com/ch32-rs/wlink),
  and a 1 × 6 pin header at 1.27 mm pitch for J3. J3 is not in the BOM,
  so the assembled boards come without it.
- A single-cell Li-ion or LiPo battery with a JST PH 2.0 plug and a
  built-in protection circuit (PCM). **Use only a protected cell.** The
  board has no battery protection, no power switch and no undervoltage
  cutoff. Without USB, Q1 connects the battery to VCC all the time, also
  in standby. A battery left on J2 keeps draining, and a short on VCC has
  no current limit.
- For the standby current: a Nordic PPK2 or another µA meter.

## The board

There are no test points. Measure on the pads of the parts below.

The names C1 to C16 are used two times on this board. The capacitors are
C1 to C5, C7 and C8 (there is no capacitor C6). The COM lines of the LED
matrix are the nets C1 to C16. This document writes "capacitor C1" or
"net C1".

| Net | What it is | Where to measure |
|---|---|---|
| VBAT | The battery, J2 pin 1 (+). The silkscreen has a `+` mark. | J2 pin 1, capacitor C3 (10 µF) |
| VBUS | USB 5 V from J1 | capacitors C1 (10 µF) and C2 (100 nF), R37 |
| VCC | The supply of everything: the CH643 (pins 31 and 64), the LEDs, the IR parts | J3 pin 5, capacitors C7 (10 µF), C8 (1 µF), C4 and C5 (100 nF at the VDD pins) |
| GND | | J3 pin 6, J2 pin 2 |

There is no voltage regulator. The CH643 (U1) runs on VCC directly:

- **On USB**, VCC comes from VBUS through D316 (1N5819WS, a Schottky
  diode). VCC is VBUS minus about 0.3 V, so about 4.7 V.
- **On the battery**, VCC comes from VBAT through Q1 (JSM2301S, a
  P-channel MOSFET). R37 (100 kΩ) pulls the gate of Q1 to GND, so Q1 is
  on. VCC is almost the same as VBAT.
- When USB is in, VBUS is on the gate of Q1, and Q1 is off. U3
  (XT4054K421, a 4.2 V linear charger) then charges the battery from
  VBUS.

Other parts in this checklist:

| Part | What it does | CH643 pin |
|---|---|---|
| U3 XT4054K421 | Charger, 4.2 V. R35 (2 kΩ) sets 500 mA ("IBAT = (1v/2k)*1000 = 0.5A" in the schematic). Its CHRG output pulls low while it charges; R34 (10 kΩ) pulls it up to VCC. | PB22 (pin 52) |
| R37 | VBUS to GND, 100 kΩ. The CH643 reads VBUS directly. | PC3 (pin 12) |
| J1, R31, R33, U2 | USB-C. R31 and R33 (5.1 kΩ) on CC1 and CC2 tell a USB-C charger to give 5 V. U2 (TPD4E05U06) is the ESD protection. | D− PC16 (43), D+ PC17 (44) |
| SW1..SW4 | Buttons A, B, C, D. Pull-ups R25, R26, R29, R30 (10 kΩ) to VCC. Low when pressed. | A PB20 (50), B PC5 (20), C PC4 (19), D PB18 (38) |
| D315, R32 | IR LED. Anode to VCC through R32 (200 Ω), cathode to the CH643. | PB21 (51) |
| S1, R28, R27 | IR receiver (DY-IRMHF384, 38 kHz). Supply from VCC through R28 (47 Ω), always on: nothing switches it off. Output pulled up by R27 (10 kΩ). | PB19 (39) |
| J3 | Debug header, 1 × 6 at 1.27 mm, not fitted: 1 RX, 2 TX, 3 SWIO, 4 SWCK, 5 VCC, 6 GND | RX PB17 (37), TX PB16 (36), SWIO PC18 (45), SWCK PC19 (47) |

**J3 pin 5 is VCC, not 3.3 V.** On USB it is about 4.7 V. Do not
connect it to the supply pin of a probe or a USB-UART adapter.

### The LED matrix

The LEDs are in a matrix of 16 COM lines (rows) by 24 PWM channels
(columns): see `docs/LEDPWM_MATRIX.md`. Each PWM channel goes through
its own 200 Ω column resistor. The rule on this board:

- PWM channel PA*n* goes through resistor R*n+1*. For example, PA0 is
  R1 and PA23 is R24.
- COM line PB*n* is net C*n+1*. For example, PB0 is net C1 and PB15 is
  net C16.

| PWM | PA0 | PA1 | PA2 | PA3 | PA4 | PA5 | PA6 | PA7 | PA8 | PA9 | PA10 | PA11 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| U1 pin | 15 | 16 | 17 | 18 | 21 | 22 | 23 | 24 | 57 | 58 | 59 | 60 |
| Resistor | R1 | R2 | R3 | R4 | R5 | R6 | R7 | R8 | R9 | R10 | R11 | R12 |

| PWM | PA12 | PA13 | PA14 | PA15 | PA16 | PA17 | PA18 | PA19 | PA20 | PA21 | PA22 | PA23 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| U1 pin | 61 | 62 | 63 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 13 | 14 |
| Resistor | R13 | R14 | R15 | R16 | R17 | R18 | R19 | R20 | R21 | R22 | R23 | R24 |

| COM | PB0 | PB1 | PB2 | PB3 | PB4 | PB5 | PB6 | PB7 | PB8 | PB9 | PB10 | PB11 | PB12 | PB13 | PB14 | PB15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| U1 pin | 25 | 26 | 27 | 28 | 29 | 32 | 33 | 34 | 35 | 40 | 41 | 42 | 46 | 48 | 49 | 54 |
| Net | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8 | C9 | C10 | C11 | C12 | C13 | C14 | C15 | C16 |

`tools/badge.py bringup` prints these for each LED (step 9).

## 1. Visual inspection

1. Examine U1 (CH643Q, LQFP-64) with a magnifier. Look for solder
   bridges between pins, and for pins that do not touch their pads.
   Make sure that pin 1 is at the mark on the silkscreen.
2. Examine the orientation of U3 (SOT-23-5), Q1 (SOT-23), D316 (the band
   is the cathode, on the VCC side) and U2.
3. Examine J1 (USB-C). Look for bridges between the fine pins (VBUS, CC,
   D+, D−).
4. Examine J2 (battery). Both signal pins and both mounting pads must be
   soldered.
5. Look along the LEDs. No LED may be missing, turned, or standing up on
   one end. The 35 RGB LEDs (NH-B1515RGBA, on the arms) have four pads.
   Pin 2 is the common anode. Compare the orientation of each one with
   the PCB in KiCad.
6. Examine D315 (IR LED), S1 (IR receiver) and SW1 to SW4.

**You should see:** clean joints, every part in its place, the right way
round.

**If not:** repair it now, before you apply power. A bridge on U1 or U3
can damage parts in step 3.

## 2. Resistance, without power

1. Set the multimeter to resistance. Measure from VCC (J3 pin 5 or
   capacitor C7) to GND.
2. Measure from VBUS (capacitor C1) to GND.
3. Measure from VBAT (J2 pin 1) to GND.
4. Set the multimeter to diode test. Put the + probe on VBUS
   (capacitor C1) and the − probe on VCC (capacitor C7). This measures
   D316.

**You should see:**

- In steps 1 to 3, no short circuit. The reading can start low and go
  up while the meter charges the capacitors. The final values are
  **(to confirm on prototype 1)**.
- In step 4, about 0.2 to 0.3 V: a Schottky diode in the forward
  direction **(to confirm on prototype 1)**.

**If not:**

- Below about 10 Ω **(to confirm on prototype 1)**: a short. Do not
  apply power. Look again at U1, U2, U3, Q1 and the capacitors C1 to
  C5, C7 and C8.
- D316 reads open, or about 0.6 V or more: D316 is turned, missing, or
  the wrong part.

## 3. First power-up, on the lab supply

1. Set the lab supply to 3.7 V. Set the current limit to 100 mA.
2. Turn the output off. Connect + to J2 pin 1 (VBAT) and − to J2 pin 2
   (GND). Do not connect USB.
3. Turn the output on. Read the current at once.
4. Measure VCC (J3 pin 5 or capacitor C7).

**You should see:**

- A small current: a few mA **(to confirm on prototype 1)**. A new
  CH643 has no firmware. Its boot ROM runs, and no LED lights.
- VCC almost the same as VBAT, about 3.7 V.

**If not:**

- The supply goes into current limit and its voltage drops: a short on
  VBAT or VCC. Turn the output off at once. Touch U1, U3, Q1, and
  capacitors C3 and C7 carefully, or use a thermal camera, to find the
  warm part.
- VCC is 0 V: there is no path from J2 to VCC. Even a Q1 that is off
  lets current through its body diode. Examine the joints of J2, and
  Q1 pins 2 and 3 (missing, or a joint that is open).
- VCC is about 0.5 V lower than VBAT: only the body diode of Q1
  conducts. Examine R37 and the gate of Q1.
- The current is tens of mA: a partial short or a turned part. Find
  the warm part.

## 4. USB power

1. Turn the lab supply off and disconnect it from J2. With USB in, the
   charger pushes current into J2, and most lab supplies cannot take
   it.
2. Connect the board to the PC with the USB-C cable. A USB power meter
   between them shows the current.
3. Measure VBUS (capacitor C1).
4. Measure VCC (J3 pin 5 or capacitor C7).
5. On a board without firmware, run `lsusb -d 1a86:55e0`.

**You should see:**

- VBUS from 4.75 to 5.25 V.
- VCC about 0.3 V less than VBUS, so about 4.7 V.
- `1a86:55e0 QinHeng Electronics` in `lsusb`. This is the CH643's USB
  boot ROM. A blank chip stays in it, so it proves USB, the clock and
  the chip before you flash.

**If not:**

- VBUS is 0 V on a USB-C charger, but correct from a USB-A to USB-C
  cable: examine R31 and R33 (5.1 kΩ on CC1 and CC2).
- VBUS is 0 V on all cables: examine J1 (VBUS pins A4, A9, B4, B9).
- VCC is 0 V: examine D316.
- No `1a86:55e0`: see step 7, "If not".

Also write down VBUS − VCC with every LED on (the factory test's LED
step, step 8). The CH643 sees VBUS directly on PC3, a little above its
own VDD. `docs/OLD_FIRMWARE.md`, "Hardware notes", explains why this
matters.

## 5. The charger

1. Measure the battery voltage. It must be from 3.0 to 4.0 V for this
   test.
2. Make sure that the + wire of the battery goes to J2 pin 1, at the `+`
   mark. JST PH batteries come with both polarities.
3. Connect the battery to J2 with an ammeter in series, or use the USB
   power meter. Do not connect USB yet.
4. Connect USB.
5. Measure CHRG at R34 (the side that is not VCC), or on U3 pin 1.
6. Leave it to charge, and measure VBAT now and then.

The charge current and the LED current both come from VBUS. Together
they can be about 800 mA. A USB 2.0 port must supply only 500 mA.
Use a USB-C charger or a USB 3 port while a battery is on J2. Do not
leave a battery on J2 when you do not use it: see "What you need".

**You should see:**

- A charge current of up to 500 mA (R35 = 2 kΩ). When the battery is
  almost full, the current goes down.
- CHRG at about 0 V while the battery charges.
- VBAT goes up and stops at 4.2 V. Then CHRG goes up to VCC.
- After step 8, the firmware shows the same: the factory test's USB
  step shows `CHG` while the battery charges. The NAMETAG app shows a
  charging screen when charging starts.

**If not:**

- No current: examine the orientation of U3, and R35.
- More than 500 mA: examine the value of R35.
- U3 gets hot. At 500 mA and VBAT = 3.7 V, U3 dissipates about
  (5 − 3.7) V × 0.5 A = 0.65 W. A charger of this type lowers the current
  when it gets too hot **(to confirm on prototype 1)**.
- Without a battery, CHRG switches on and off again and again. That is
  normal for this charger with no battery: the firmware filters it
  (#71).

## 6. First flash

Use one of the two ways. Over USB, you need no probe and no header.
Flash `factory.bin`, not `ledmatrix.bin`: only the factory image has the
factory test (step 8) in its store.

### Over USB

A blank chip stays in the boot ROM (step 4), so you need no button.

1. Connect the board to USB. Make sure that `lsusb -d 1a86:55e0` shows
   the boot ROM.
2. Flash:

   ```sh
   ch32fun/minichlink/minichlink -w factory.bin flash -b
   ```

   This writes up to the top of the flash **(to confirm on prototype 1)**.
   If minichlink reports `Fault writing image`, flash `ledmatrix.bin`
   the same way, then run `tools/badge.py factory` (the factory test
   from the store, with `make -C apps` first).

### Over SWD, with a WCH-LinkE

Use this way when the boot ROM does not show up, or on a board whose
firmware does not start.

1. Connect the WCH-LinkE to the PC. `lsusb` must show `1a86:8010` (RISC-V
   mode). If it shows `1a86:8012`, run `wlink mode-switch --rv` (see
   `README.md`, "Flash with a WCH-Link probe", step 1).
2. Solder the 1 × 6 header on J3.
3. Connect the probe's GND to J3 pin 6 and its SWDIO to J3 pin 3
   (SWIO). **Do not connect the probe's 3V3 to J3 pin 5**: pin 5 is VCC,
   about 4.7 V on USB.
4. Connect the board to USB. Flash only on USB power: on prototype 1,
   flashing failed on PPK2 power and left the chip half written.
5. Flash as `README.md`, "Flash with a WCH-Link probe", says:

   ```sh
   wlink --speed low --chip CH643 reset   # must print "Attached chip: CH643"
   wlink --speed low --chip CH643 flash --address 0x08000000 factory.bin
   ```

   minichlink also knows the CH643 and the WCH-LinkE
   (`ch32fun/minichlink/minichlink -w factory.bin flash -b`)
   **(to confirm on prototype 1)**.
6. To read the firmware's log, keep the probe on and run
   `ch32fun/minichlink/minichlink -T`.

**You should see:** the flash tool reports success. The badge restarts,
and the factory test starts by itself: `LED` on the grid, then every LED
on.

**If not:**

- `WCH-Link underlying protocol error: 0x55`: no contact on a pin, or no
  power. Examine the wires and USB power. Use `--speed low`, and reset
  first.
- A flash that stopped half way can leave the chip in a state where the
  boot ROM does not start by itself. Erase and flash again over SWD, on
  USB power: `wlink --speed low --chip CH643 erase`, then the flash
  command. An erase also erases the stored items, the name and the
  settings.

## 7. USB enumeration

1. Connect the flashed board to USB. Run `lsusb -d 1209:d643`.
2. Run `tools/badge.py list`.
3. Open the menu (hold **A**), step to **UPDATE** with C or D, and tap
   **A**. At once, run `lsusb -d 1a86:55e0`. With pull request #77 you
   can also hold **C + D** for 3 s.
4. Wait. Do not flash.

**You should see:**

- In step 1: a line with `ID 1209:d643`. `lsusb -v -d 1209:d643` shows
  `Badge.Team` and `Hackerhotel 2027 starfish`.
- In step 2: `mode ...`, the number of items, the free space and the
  app API.
- In step 3: the boot ROM, `1a86:55e0`. If no host talks to it, it
  starts the firmware again after about 5 s (step 4).

**If not:**

- No device: examine D+ and D− at U1 pins 44 and 43, at U2, and at J1
  (A6, A7, B6, B7). USB works only on USB power: the CH643's USB uses
  its 5 V setup, with VDD from VBUS.
- `dmesg` shows `device descriptor read` errors: USB runs from the
  CH643's internal oscillator (HSI, −2.0 to +1.8 % from 0 to 70 °C,
  CH643 datasheet, table 3-9). Suspect the clock first.
- `tools/badge.py` says `the badge is in use`: close or disconnect the
  web interface.

## 8. The factory test

1. Power the badge from USB. After the first flash with `factory.bin`, the
   test starts by itself. Else run `tools/badge.py factory`: it stores the
   test app and starts it.
2. Do the steps on the card in `docs/FACTORY_TEST.md`.
3. Before the LED step, disconnect the battery from J2, or wait until
   CHRG is high (step 5). Else the meter also shows the charge current.
4. During the LED step (every LED on, at full brightness), read the
   current on the USB power meter, and measure VCC and VBUS (step 4).

**You should see:** the green arms and `OK`. Then the name tag.

The current with every LED on is **(to confirm on prototype 1)**. One
COM line can drive 24 LEDs at once. With 200 Ω column resistors that is
about 13 mA each on USB, so the peak is about 320 mA **(an estimate, to
confirm on prototype 1)**. The CH643Q allows 550 mA in total through its
VDD pins (CH643 datasheet, table 3-1, IVDD).

**If not:** go to the step of this checklist for the part that failed:

| Factory step | Go to |
|---|---|
| LED or ROW: a dark LED, a wrong colour, two lines at once | step 9 |
| KEY | step 10 |
| USB, `CHG` | steps 4 and 5 |
| IR, `IR?` | step 11 |

## 9. The LED walk

`tools/badge.py bringup` lights one LED at a time over USB. For each LED
it prints the designator, the colour, the kind (`grid`, `edge` for the
outline, `rgb` for the arms), the COM line, the PWM channel, the column
resistor, the slot and the place on the badge. It needs no special
firmware.

1. Finish the factory test first, or leave it: hold **A + B** for 3 s.
2. Close the web interface. Only one program can use the badge.
3. Run:

   ```sh
   tools/badge.py bringup
   ```

4. Look at the badge after each line. Press Enter to go on. Type `b`
   and Enter to go back. Type `x` and Enter to mark the LED as bad and
   go on. Type `q` and Enter to stop.
5. At the end, the tool lists the LEDs you marked. Then the badge shows
   its name tag again. Ctrl-C does the same.

Useful options:

| Option | What it does |
|---|---|
| `--from D35` or `--from 120` | start at a designator or at a step number |
| `--kind grid`, `edge` or `rgb` | only the centre grid, the outline, or the RGB LEDs (red, green and blue one after the other) |
| `--lines rows` | one whole COM line at a time, PB0 to PB15 |
| `--lines cols` | one whole PWM channel at a time, PA0 to PA23 |
| `--auto 0.5` | go on by itself every 0.5 s |
| `--count 10` | stop after 10 steps |

The walk goes through the centre grid row by row, then the outline
clockwise from the top, then the arms. If the LEDs are dim, press **B**
on the badge to make them brighter. With pull request #78, a new badge
starts at a low brightness: `tools/badge.py brightness 255`.

**You should see:** at each step, one LED at the place that the line
gives, in the colour that it gives.

**If not:**

| What you see | Examine |
|---|---|
| Nothing lights | That LED: orientation, solder joints |
| Another LED lights | Both designators: a part in the wrong place, or a bridge |
| Two LEDs light | A short between their lines. Run `--lines rows` and `--lines cols` |
| A whole COM line stays dark in `--lines rows` | U1's pin for that PB line, and net C*n+1* |
| A whole PWM channel stays dark in `--lines cols` | The column resistor R*n+1*, and U1's pin for that PA line |
| A line lights LEDs of another line too | A short between those two lines |
| An RGB LED stays dark in all three colours, or lights in a step of another LED | The RGB LED is turned: its common anode (pin 2) is then on a COM net **(to confirm on prototype 1)** |
| An RGB LED shows the wrong colour, or two colours swap | A bridge between its COM nets, or the wrong part |

## 10. Buttons

1. Make sure that the factory test's KEY step passed (step 8).
2. Hold **A**. The menu opens. Press **C** and **D**: the entry changes.
   Hold **A** again to close it.
3. Outside an app, press **B** and **D**: the badge gets brighter and
   dimmer.

**You should see:** every button does its job.

**If not:** measure the button's pin (see "The board"). It must be at
VCC when released and at 0 V when pressed. Examine the switch, and its
pull-up: R25 (A), R26 (B), R29 (C) or R30 (D).

## 11. Infrared

1. Make sure that the factory test's IR step passed (step 8). There the
   receiver hears the badge's own IR LED.
2. Run `tools/badge.py irlog`. Point a TV remote (NEC) or a Hacker Hotel
   2025 banana at the badge, and press a button on it.
3. Run `tools/badge.py shoot 1`. Look at D315 through a phone camera:
   most phone cameras show IR light. Or run `irlog` on a second badge.
4. With two badges: give each one a name (`tools/badge.py name ALICE`,
   `tools/badge.py name BOB`), and install and start the NAMETAG app on
   both (`tools/badge.py add apps/build/nametag.hhapp`). The app sends
   nothing without a name. Keep both badges near each other for 20 s,
   then tap **C** on one of them.

**You should see:**

- In step 2, frames with a leader and bits.
- In step 3, D315 flashes once.
- In step 4, the panel that C opens shows `NEAR 1` and the other
  badge's name. The SWIO log (`minichlink -T`, section 6) shows
  `nametag: new <name>` the first time a badge meets the other one. The
  `HI <name>` pop-up shows only at a first meeting (often before you tap
  C), and not again for a name the badge already knows, also after a
  power-off. If step 4 fails but step 3 passed, use `irlog` on the
  second badge as the IR check.

**If not:**

- Nothing received: examine the orientation of S1, R28 (supply), R27
  (pull-up). PB19 must be high (VCC) when no IR arrives.
- D315 does not flash: examine its orientation and R32. PB21 sinks the
  current: about 12 mA on the battery and 17 mA on USB, with 200 Ω
  **(an estimate, to confirm on prototype 1)**.

## 12. Standby current

This is issue #14. The CH643 datasheet gives 55 µA typical for the chip
in standby, with the watchdog and AWU off (table 3-8). The old firmware
drew 2.84 mA on prototype 1.

The badge cannot get down to the chip's figure. The IR receiver S1 is
always on: R28 connects it to VCC, and nothing switches it off. Its shop
listing gives a supply current of up to 2.5 mA. S1 can account for
most of the 2.84 mA.

1. Disconnect USB and the SWD probe. Disconnect the battery from J2.
2. Set the PPK2 to source meter, 3.7 V. Connect it to J2 as a battery:
   VOUT to pin 1, GND to pin 2.
3. Hold **A**, step to **SLEEP**, and tap **A**. The badge goes dark.
4. Read the average current over 10 s.
5. Press a button. The badge wakes up.

**You should see:** the current of S1 plus the CH643's standby
current. Expect up to about 2.5 mA **(to confirm on prototype 1)**.
Write the value in issue #14.

**If not:**

- More than about 2.6 mA: examine S1 and R28 first. Then the pads of
  the CH643 that float: gap 7 in `docs/OLD_FIRMWARE.md` lists them.
- For the CH643 alone, lift R28 and measure again. Then S1 gets no
  power, and you should see about the datasheet's 55 µA.
- Use the PPK2 only to measure: do not flash on PPK2 power (step 6).

## When a board passes

The board is good. Write down what you measured where this document says
"to confirm on prototype 1", and update this document.

[old]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/BRINGUP.md
