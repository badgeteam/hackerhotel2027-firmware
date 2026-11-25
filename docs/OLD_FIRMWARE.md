# The old firmware, feature by feature

This firmware took over from the previous one,
[annejan/hackerhotel2027-firmware][old] (Rust in `src/`, C in `c/src/`,
now archived). This file lists what the old firmware had, area by area,
and where each feature stands in this firmware. It answers issue
[#20][i20].

The comparison is between the old firmware at commit 7e0e68b and this
firmware at commit ea68a00 (November 2026). Nothing in this file was
tried on a badge for this comparison.

The old firmware had everything in the firmware: 6 animations, 11 games,
the challenges, the factory test and the sponsor reel, in 47.6 KB (C).
This firmware keeps the firmware under 30 KB and makes most features apps
in the item store (see `README.md`, "Flash and RAM").

The status column uses these words:

- **done**: here, in the firmware or as an app, with the pull request
  (#NN) or commit.
- **replaced**: here, but done another way. The notes say how.
- **partly**: some of it is here. The notes say what is missing.
- **tracked**: not here yet. An open issue has it.
- **untracked**: not here, and no issue has it. See
  [Untracked gaps](#untracked-gaps).

Each #NN is a link to issue or pull request NN of this repository
(issues and pull requests share the numbers).

## Button names

The old firmware named the buttons by where your fingers are when you
hold the badge from the front. This firmware uses the silkscreen letters.
The same letter is a different switch:

| Old name | Switch | Pin | Now |
|---|---|---|---|
| ◀ | SW3 | PC4 | **C** |
| ▶ | SW4 | PB18 | **D** |
| A | SW2 | PC5 | **B** |
| B | SW1 | PB20 | **A** |

So "hold A for the menu" is the same words, but now it is SW1. The tables
below give the old names in the first column and the new letters in the
notes.

## Buttons and chords

| Old feature | Status now | Notes |
|---|---|---|
| Hold A (0.6 s): the menu, also in a game ([USAGE][u-always]) | done: [#34] | Hold A 0.7 s, or 1 s in an app. The app also gets that press of A. |
| Tap, hold and chord rules: a chord never taps or holds ([USAGE][u-buttons]) | partly | A held together with another button still opens the menu. Each app reads `pressed()` and `held()` and makes its own rules. |
| Hold ◀ + ▶ for 3 s: reboot into the USB boot ROM, after an arrow runs down the grid for a second ([USAGE][u-always]) | replaced: [#34], [#33] | UPDATE in the menu, or the firmware update in the web interface (`CMD_BOOT_ROM`). There is no chord and no arrow. |
| The same chord, held while USB is plugged in ([BRINGUP][bringup], steps 3 and 10) | untracked | To reach the boot ROM, the menu must work. A button held at power-on is now the safe start: the badge skips the boot item ([#1]). |
| Hold A + B for 3 s: the bring-up test ([USAGE][u-bringup]) | partly: [#59] | The same two switches now only leave the factory test. See [Bring-up test](#bring-up-test). |
| Tap A on the name tag or an animation: brightness ([USAGE][u-nametag]) | replaced | Outside apps, B is brighter and D dimmer. |
| ◀ / ▶ on the name tag or an animation: previous / next screen ([USAGE][u-nametag]) | untracked | Only the menu changes what runs. C does nothing outside apps. |

## Menu and settings

| Old feature | Status now | Notes |
|---|---|---|
| The menu: label on the grid, place on the outline, group on the arms, 20 s timeout ([USAGE][u-menu]) | done: [#34] | Groups now: orange the name tag and SAY HI, purple animations, blue texts, green apps, white settings. C / D step; the new BUTTONS setting makes it B / D. |
| Fixed entries: NAME, HACKER HOTEL, 6 animations, 11 games, VICTORY, EDIT NAME, LIGHT, SPONSORS, SLEEP | replaced: [#1], [#34] | The menu lists the items in the store. The animations and games are apps in `apps/`. A new badge gets the recommended set (`apps/recommended.txt`, **Erase badge and install recommended apps** in the web interface's App library). It holds 3 of the 6 old animations and 6 of the 11 old games. The rest are in the web interface's App library. For VICTORY and SPONSORS, see [Challenges](#challenges-and-the-victory-dance) and [Sponsor reel](#sponsor-reel-charging-and-standby). |
| LIGHT: 5 levels (night, 5 %, 12.5 %, 31 %, full), a bar on the grid, 12.5 % at first start ([USAGE][u-menu]) | partly: [#34], [#48] | BRIGHTNESS has 6 levels (8, 16, 32, 64, 128, 255). The grid shows only the label, not the level. A new badge starts at full brightness. |
| SLEEP: dark until a button; that button only wakes; USB disconnects | done: [#34], `standby.c` | The same behaviour. The standby current is not measured: [#14]. |
| New in this firmware | done | BUTTONS ([#34]), UPDATE ([#34]), SAY HI ([#37]), safe start ([#1]). |

## Name tag and your name

| Old feature | Status now | Notes |
|---|---|---|
| Name tag: up to 3 characters stand still, longer names scroll ([USAGE][u-name]) | done: [#29] | Still if it fits, else it scrolls. |
| Challenge progress on the name tag's arms ([USAGE][u-challenges]) | tracked: [#6] | The name tag's arms now show banana shots ([#66]). |
| EDIT NAME: ◀ / ▶ character (hold repeats), tap A next position, B previous, hold A saves, 1 min without a button closes without a save ([USAGE][u-editor]) | done: [#34] | Now C / D, tap A, B, hold A. Up to 32 characters, not 24. Lower case ([#31]) and `& + _` are new. |
| Default name at build time (`BADGE_NAME=`, `make NAME=`) ([USAGE][u-default]) | replaced: [#29] | Without a name, the name tag shows "HACKER HOTEL 2027". Set the name in the web interface, with `tools/badge.py name` or with EDIT NAME. |
| Name in flash, two pages in turn, at most one write every 5 s ([USAGE][u-editor]) | replaced: [#2] | The persistent record is kept twice and written 1 s after the last change (`persist.c`). |
| HACKER HOTEL screen ([USAGE][u-menu]) | done: [#49] | `apps/hotel`. |

## Animations

Only ripple, galaxy and fire are in the recommended set
(`apps/recommended.txt`). Install the others from the web interface's App
library.

| Old feature | Status now | Notes |
|---|---|---|
| Ripple, galaxy, heartbeat, life, fire, twinkle ([`src/anim.rs`][anim]) | done: [#49] | `apps/ripple`, `galaxy`, `heart`, `life`, `fire`, `twinkle`. B / D change the brightness. |
| The charging animation | tracked: [#9] | See [Sponsor reel, charging and standby](#sponsor-reel-charging-and-standby). |

## Games

Snake, Startris, Breakout, Flappy, Simon and 4 in a row are in the
recommended set (`apps/recommended.txt`). Pong, Star Chase, Whack-a-mole,
Maze and Higher lower are not: install them from the web interface's App
library.

| Old feature | Status now | Notes |
|---|---|---|
| Demo mode until ◀, ▶ or B ([USAGE][u-demo]) | partly: [#45] | Until B, C or D. The demo comes back 15 s after a game, not at once. 4 in a row has no demo: while it is open, it looks for a partner badge. The old one had a demo, and a partner badge could join from it. |
| Game over: red arms, the score on the grid | replaced | Each app shows its own end screen. The apps also keep the best score with `save()`, which the old firmware did not do. |
| Leaving a game: hold A; the game also gets that press ([USAGE][u-leave]) | done: [#34] | Hold A for 1 s in an app. |
| Snake: ◀ / ▶ turn | done: [#1] | `apps/snake`. C / D turn. |
| Startris: ◀ / ▶ move, A rotates, B drops | done: [#43] | C / D move, tap A rotates, B drops. |
| Breakout: ◀ / ▶ move the paddle | done: [#41] | C / D. |
| Pong: ◀ up, ▶ down | done: [#42] | B up, D down. Not in the recommended set. |
| Flappy: ◀, ▶ or B flaps | done: [#42] | B, C or D. |
| Star Chase | done: [#44] | B, C or D. Not in the recommended set. |
| Whack-a-mole, Simon | done: [#39] | Simon uses the arms next to the four buttons. Whack-a-mole is not in the recommended set. |
| Maze: ◀ / ▶ left / right, B up, A down | done: [#44] | A up, B right, D down, C left. Not in the recommended set. |
| Higher lower: ▶ higher, ◀ lower | done: [#44] | D higher, C lower. Not in the recommended set. |
| 4 in a row, against the badge or another badge | done: [#38] | `apps/four`. See [Infrared](#infrared). |

## Challenges and the victory dance

The old firmware made five games into challenges, one per arm, with five
levels each ([USAGE][u-challenges], [`src/challenge.rs`][challenge]).
None of this is here yet. Issue [#6] tracks it. Each game app keeps only
its own best score, in its own 28 bytes. Of the five games, only Simon
and Snake are in the recommended set (`apps/recommended.txt`). Maze,
Whack-a-mole and Higher lower are in the web interface's App library, so
a badge with only the recommended set cannot play all five challenges.

| Old feature | Status now | Notes |
|---|---|---|
| Maze (top arm, blue): 1, 2, 3, 4, 5 mazes | tracked: [#6] | `apps/maze` keeps the most mazes solved. |
| Simon (upper right, yellow): length 4, 6, 8, 10, 12 | tracked: [#6] | `apps/simon` keeps the best length. |
| Whack-a-mole (lower right, red): 20, 40, 60, 80, 100 points | tracked: [#6] | `apps/whack` keeps the best score. |
| Higher lower (lower left, purple): streak 3, 5, 7, 9, 12 | tracked: [#6] | `apps/hilo` keeps the longest streak. |
| Snake (upper left, green): 5, 10, 15, 20, 30 food | tracked: [#6] | `apps/snake` keeps the best score. |
| Only a real game counts, never the demo; a worse game never takes a level away | partly | The apps never save a demo score. |
| Progress kept in flash, over power-off and firmware updates | tracked: [#6] | Needs room in the persistent record, shared between apps. |
| The victory dance at 25 / 25, and VICTORY in the menu | tracked: [#6] | Could be an app. |

## Infrared

| Old feature | Status now | Notes |
|---|---|---|
| Packets: NEC-style pulse distance on 38 kHz, with a type, a length and a CRC ([README][r-ir]) | done: [#37] | `ir_codec.c`, up to 32 data bytes. Apps get `ir_send_packet()` and `ir_receive_packet()` (app API 5). |
| Name swap: B on the name tag sends the name; the other badge says HI and sends its name back ([USAGE][u-ir]) | done: [#37] | SAY HI in the menu. 4 tries in 6 s, then `NO REPLY`. Issue [#8] is still open. |
| A greeting that arrives while the menu, the editor or a game is open waits, and shows after | partly: [#37] | A badge that runs an app does not answer: the app gets the packets. |
| 4 in a row between two badges: 0.8 s resend, a check every 3 s, `link lost` after 20 s, the moves in the log ([USAGE][u-four]) | done: [#38] | The same timings. The log goes out over SWIO. |
| New in this firmware | done | Banana and Time Blaster shots ([#54]), the banana app ([#55]), the raw IR log (`tools/badge.py irlog`, [#53]). |

## USB and the setup page

The old setup page (`docs/webusb/index.html`, [USAGE][u-setup]) became
the web interface (`webui/`), which does much more: animations, texts,
apps, the item store and firmware updates.

| Old feature | Status now | Notes |
|---|---|---|
| Set the name, saved in flash | done: [#2] | Web interface and `tools/badge.py name`. |
| Pick what the badge shows | replaced: [#1] | **Start** next to the item in the web interface, or `tools/badge.py run N`. |
| Set the brightness | untracked | The status line shows the brightness, but the USB protocol has no command to set it. |
| Read the state every second: what shows, the brightness, charging | partly | The page asks every second: mode, brightness, store, app API. There is no charging bit ([#9]). |
| Windows: Microsoft OS 2.0 descriptor, WinUSB, no driver to install ([README][r-setup]) | untracked | `usb_config.h` has no BOS descriptor (bcdUSB 2.00). Not checked on Windows. |
| USB serial number made from the chip's unique ID ([BRINGUP][bringup], step 2) | untracked | The serial number is always `001`. |
| USB ID 1209:D035 | replaced | Now 1209:D643. |

## UART console

The old firmware had a log and a console on header J3 (115200 8N1,
[USAGE][u-uart]). This firmware has no UART: the log goes out over SWIO
(`minichlink -T`). Issue [#10] tracks the console. Issue [#22] (DMX) also
wants the UART's RX pin.

| Old command | Status now | Notes |
|---|---|---|
| `help` | tracked: [#10] | |
| `info` | partly | `tools/badge.py list` gives the items, what runs, the boot item and the app API. |
| `name TEXT` | replaced: [#2] | `tools/badge.py name TEXT`. |
| `show N` | replaced: [#1] | `tools/badge.py run N`. |
| `level N` | untracked | No way to set the brightness over USB (see above). |
| `hello` | partly: [#37] | SAY HI in the menu. No USB command. |
| `test` | tracked: [#11] | |
| `factory` | replaced: [#59] | `tools/badge.py factory` (USB command `0x52`). |
| `factory skip` | untracked | |
| `sponsors` | tracked: [#12] | |
| `progress` | tracked: [#6] | |
| The log: boot, names saved, IR packets | replaced | Over SWIO. `tools/badge.py irlog` shows the raw IR ([#53]). |

## Factory test

The old test is in [USAGE][u-factory] and [`src/factory.rs`][factory].
This one is in `docs/FACTORY_TEST.md`.

| Old feature | Status now | Notes |
|---|---|---|
| Runs at every power-on until it passes once; kept in flash | done: [#59] | Not in the emulator, which skips it at power-on. |
| LED and ROW: the volunteer taps B (good) or A (bad) | replaced: [#62] | Both run by themselves. A bad LED or row means the repair pile. |
| KEY, fails after 20 s | done: [#59] | `make FACTORY_KEYS=0` leaves it out ([#62]). |
| USB, fails after 10 s | done: [#59] | Also shows `CHG` while the battery charges. |
| IR, then `IR?` after 10 s; B passes it anyway, A fails it | partly: [#62] | `IR?` after 10 s, a fail after 30 s. There is no way to pass IR untested. |
| A failed step: tap B to try it again | done: [#59] | |
| Hold A + B for 3 s: on to the bring-up test | partly: [#59] | Leaves the test without a pass. It runs again at the next power-on. |
| The log on the UART, with hints for the volunteer | replaced: [#59] | Over SWIO, for example `factory FAIL KEY: missing: . . C D`. |
| The card for the flashing table ([USAGE][u-card]) | done: [#59] | In `docs/FACTORY_TEST.md`. |

## Bring-up test

The old bring-up test ([USAGE][u-bringup], [`src/selftest.rs`][selftest])
checked a new board step by step, with a log on J3. Issue [#11] tracks it.
The factory test now covers some of the steps.

| Old step | Status now | Notes |
|---|---|---|
| COM: all 24 PWM channels on one COM line, PB0..PB15 in turn | partly: [#59] | The factory ROW step walks the rows and columns in the order you see, not in wire order. |
| PWM: one PWM channel on all COM lines, PA0..PA23 in turn | partly: [#59] | The same. |
| LED: one LED at a time, with its designator in the log (`slot 41: PB1 PA17 D35 green`) | tracked: [#11] | `webui/board_data.js` has the designators (`ref`), so a host tool could walk the LEDs over USB. |
| ALL: every LED on at the current brightness, to measure the current | partly: [#59] | The factory LED step lights every LED, but only for 4.5 s. |
| GRY: a grey ramp on the grid, the arms in five colours | tracked: [#11] | |
| KEY: the buttons held | done: [#59] | The factory KEY step. |
| SCN: LED rows per second (2930 rows/s at a 48 MHz LED clock) | tracked: [#11] | |
| IR: own echo, heard of sent | done: [#59] | The factory IR step (`infrared_set_loopback`). |
| PWR: USB or battery, and charging | partly: [#59] | The factory USB step shows USB and `CHG`. |

## Sponsor reel, charging and standby

| Old feature | Status now | Notes |
|---|---|---|
| Sponsor reel: once, at the first power-on after the factory test; "THANKS TO", then each logo with the arms in the sponsor's colours ([USAGE][u-sponsors]) | tracked: [#12] | The logos are in the old repository's `assets/sponsors`, the tool is its `tools/sim/logo.py`. |
| SPONSORS in the menu, `sponsors` on the console | tracked: [#12] | An animation item in the store can show logos now; playing it once needs the firmware. |
| Charging screen: a battery that fills, over and over, while USB charges; a tap hides it; not over a game or the menu ([USAGE][u-always]) | tracked: [#9] | `Power_Charging()` reads CHRG (PB22), but only the factory test uses it. Apps have no `charging()`. |
| Standby until a button; the button only wakes | done: `standby.c`, [#34] | |
| Unused pads pulled up, so they draw no current ([README][r-hw]) | untracked | No code in this firmware pulls up the unbonded or unconnected pads. This can matter for [#14]. |

## Emulator and tools

| Old feature | Status now | Notes |
|---|---|---|
| The firmware on the PC, the buttons drawn on the board ([USAGE][u-emu]) | done: [#3] | Apps run in a RISC-V interpreter. Keys A, B, C, D, or click. |
| `C`: USB power on and off (charging) | untracked | `tools/emu/hw.c`: the emulated badge is always on USB and never charges. |
| `R`: reset | done: [#3] | `R` powers off and on; a key held then is held at power-on (safe start). |
| A name kept over `R`, lost when the emulator closes | done: [#3] | The flash is a file, so everything stays. |
| `--fresh`: the factory test, then the sponsor reel | untracked | The emulator skips the factory test at power-on. `tools/factory_test` starts it over USB. |
| One shared infrared room (UDP 127.255.255.255:8028) | done: [#3], [#37] | |
| stdin is the UART console | tracked: [#10] | stdin carries the emulator's own protocol now. |
| `--web`: the setup page on localhost, any browser | done: [#3] | The whole web interface. |
| `parity.py`: C and Rust frame by frame | replaced | One firmware now. |
| `ir_game.py`: 4 in a row on two emulators, also with 30 % of packets lost | partly: [#38] | `tools/four_test` plays whole games on two emulators, without lost packets. |
| USB protocol fuzz test, 200 000-frame soak | partly | `tools/app_test` gives every app random buttons in the emulator. No USB fuzz test. |
| `tools/sim/docs_gifs.sh`: GIFs of every screen for the docs | untracked | The docs here have no GIFs. |
| CI fails the build over 61440 bytes, the most the `wchisp` command-line tool can flash without harm there ([README][r-tools]) | untracked | The linker stops at 30720 B (`ledmatrix.ld`). The `wchisp` command-line tool can erase the start of the item store from 29697 B (see [Hardware notes](#hardware-notes)). Nothing checks for that. The firmware is 27964 B now. |
| `tools/gen_layout.py` from the KiCad board | partly | Now from `webui/board_data.js`; the script that makes that file is missing ([#13]). |

## Hardware notes

These notes are from the old firmware's [README][r-hw] and
[BRINGUP.md][bringup]. They are from the old firmware, not re-checked.
Some may be out of date for the current hardware revision.

- **Debug header J3**, 1 × 6 at 1.27 mm: 1 RX (PB17), 2 TX (PB16),
  3 SWIO (PC18), 4 SWCK (PC19), 5 VCC, 6 GND. Pin 5 is the badge's VCC
  (battery or USB, up to about 5 V), not 3.3 V: do not connect it to an
  adapter's supply. J3 is not in the BOM, so solder a header on.
- **Boot ROM.** It shows up as 1a86:55e0 and waits about 5 s for a host.
  After a host has talked to it, it stays. A blank chip (first word of
  the application flash 0xFFFFFFFF) stays in ISP mode. By pins, it enters
  ISP only when PC17 is high and PC16 low, and nothing on the board pulls
  PC17 high.
- **The `wchisp` command-line tool** ([ch32-rs/wchisp][wchisp], not
  `webui/wchisp.js`) erases whole 1 KB sectors, one more than the image
  needs. Here the item store starts at 30 KB (0x7800), so a firmware image
  over 29 KB (29696 B) flashed with that tool can erase the start of the
  store. The web interface's firmware update (`webui/wchisp.js`) and
  minichlink erase only the sectors the image needs, from address 0, at
  least 8. The web update refuses an image or an erase that reaches
  0x7800 (`docs/FIRMWARE_UPDATE.md`).
- **Unused pads.** The LQFP64 package does not bond out PC6–PC9, PC12,
  PC13 and PC20. If they float, they draw current, so pull them up. The
  board leaves PC0–PC2, PC14 and PC15 unconnected. PC3 (VBUS sense) must
  stay a floating input.
- **VBUS goes straight into PC3.** While USB is in, PC3 is a little above
  VDD. The datasheet allows at most VDD + 0.3 V on a pin. A divider or a
  series resistor is safer.
- **IR receiver supply and IR LED current:** fixed in hardware 0986add.
  The IR LED's anode is on VCC through R32 (200 Ω, about 15 mA), and
  PB21 sinks its cathode.
- **The wiring order is not the order you see.** Grid column 7 (D143)
  goes to PA9, columns 8 and 9 (D141, D142) to PA7 and PA8. Grid row 7 is
  on PB15. Only a test that walks the wires shows this.
- **No battery gauge.** The badge can tell that the battery charges, not
  how full it is. Asked for: VBAT through 2 × 1 MΩ, with 100 nF on the
  middle, to PC0 (ADC channel A10), which is free.
- **USB PHY.** Always use the 5 V setup (its own 3.3 V regulator, 10k on
  D+): USB only works when VDD comes from VBUS. This firmware does that
  (`FUNCONF_USE_5V_VDD`).
- **USB clock.** USB runs from the HSI, specified at ±2 %. If enumeration
  is flaky, suspect the HSI first; it can be trimmed.
- **LED current.** Since hardware f6015de the column resistors are
  200 Ω. One COM line can carry 24 columns, so the peak is up to about
  320 mA. The datasheet allows 550 mA in total through the VDD pins.
- **Early silicon.** Chip ID (0x1FFFF704) bits 7:4 are the silicon
  revision. On revision 0, GPIO CFGHR cannot be read back.
- **Standby current.** The old Rust firmware drew 2.84 mA in standby,
  not the datasheet's 55 µA ([#14]).

## Untracked gaps

Candidates for new issues. None of them is opened yet. Each needs a
decision first: is it worth the flash, and is it worth the work?

1. **Brightness over USB.** A USB command to set the brightness, in the
   web interface and in `tools/badge.py`. The old setup page and
   `level N` had it.
2. **Brightness on the badge.** A lower start brightness for a new badge
   (the old firmware started at 12.5 %), and the level on the grid in
   BRIGHTNESS.
3. **Browse without the menu.** C (or C / D) on the name tag steps
   through the items, as ◀ / ▶ did.
4. **A boot ROM chord.** C + D for 3 s reboots into the boot ROM, so a
   badge with a broken menu can still be flashed over USB.
5. **WinUSB on Windows.** A BOS descriptor with the Microsoft OS 2.0
   descriptor, so Windows needs no driver install.
6. **A unique USB serial number**, from the chip's unique ID, so two
   badges on one PC can be told apart.
7. **Pull up the unused pads** (PC6–PC9, PC12, PC13, PC20, and PC0–PC2,
   PC14, PC15). Could be part of [#14].
8. **Factory test overrides.** A way to pass IR untested, as B on `IR?`
   did, and to mark the test as passed (`factory skip`). This is a
   decision for the organisers of the flashing table.
9. **Emulator: USB and charging.** A key that turns USB power on and off
   and makes the badge charge, for [#9] and the factory test's USB step.
10. **Emulator: a fresh start** that runs the factory test at power-on.
11. **Bring-up from the host.** A tool that walks the LEDs one at a time
    over USB and prints each designator from `webui/board_data.js`. It
    needs no firmware bytes. Could be part of [#11].
12. **A bring-up checklist** for this firmware, as the old BRINGUP.md
    was: what to measure on a new board, and in which order.
13. **More tests:** a USB protocol fuzz test, and 4 in a row on two
    emulators with lost packets.
14. **GIFs in the docs**, recorded from the emulator.
15. **A chord rule for the menu:** hold A opens the menu only when A is
    the only button down. Probably not needed.
16. **A build-time default name.** Probably not needed, since the web
    interface and EDIT NAME set the name.
17. **A firmware size guard for the `wchisp` command-line tool.** A CI
    check that fails a firmware over 29696 B, as the old CI did at
    61440 B, or a warning in `README.md` that the tool is not safe for an
    image over 29 KB. The linker allows up to 30720 B.
18. **The J3 pinout in a current doc.** The pinout and the warning that
    pin 5 is VCC, not 3.3 V, are only in [Hardware notes](#hardware-notes)
    here. `README.md` or a hardware doc should have them, next to the
    SWIO log (`minichlink -T`).

[old]: https://github.com/annejan/hackerhotel2027-firmware
[i20]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/20
[bringup]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/BRINGUP.md
[anim]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/src/anim.rs
[challenge]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/src/challenge.rs
[factory]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/src/factory.rs
[selftest]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/src/selftest.rs
[r-ir]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/README.md#infrared-and-the-uart-console
[r-setup]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/README.md#setup-page-webusb
[r-hw]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/README.md#hardware-notes-found-along-the-way
[r-tools]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/README.md#tools
[wchisp]: https://github.com/ch32-rs/wchisp
[u-buttons]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#the-buttons
[u-always]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#always-available
[u-menu]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#the-menu
[u-nametag]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#name-tag-hacker-hotel-and-the-animations
[u-demo]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#demo-mode
[u-leave]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#leaving-a-game
[u-challenges]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#challenges
[u-name]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#your-name
[u-editor]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#typing-it-on-the-badge
[u-default]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#the-default
[u-ir]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#meeting-other-badges-infrared
[u-four]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#4-in-a-row-against-another-badge
[u-uart]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#uart-console
[u-factory]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#factory-test-the-flashing-table
[u-card]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#card-for-the-flashing-table
[u-sponsors]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#sponsor-reel
[u-bringup]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#bring-up-test-first-boards
[u-setup]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#setup-page
[u-emu]: https://github.com/annejan/hackerhotel2027-firmware/blob/main/docs/USAGE.md#emulator
[#1]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/1
[#2]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/2
[#3]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/3
[#6]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/6
[#8]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/8
[#9]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/9
[#10]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/10
[#11]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/11
[#12]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/12
[#13]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/13
[#14]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/14
[#22]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/22
[#29]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/29
[#31]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/31
[#33]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/33
[#34]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/34
[#37]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/37
[#38]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/38
[#39]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/39
[#41]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/41
[#42]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/42
[#43]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/43
[#44]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/44
[#45]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/45
[#48]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/48
[#49]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/49
[#53]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/53
[#54]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/54
[#55]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/55
[#59]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/59
[#62]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/62
[#66]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/66
