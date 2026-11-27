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
| Hold ◀ + ▶ for 3 s: reboot into the USB boot ROM, after an arrow runs down the grid for a second ([USAGE][u-always]) | done: [#77] | Hold C + D for 3 s: on the name tag, in the menu, in an app and in the factory test. The same arrow, then the boot ROM. UPDATE in the menu ([#34]) and the web interface's firmware update ([#33]) also reach it. |
| The same chord, held while USB is plugged in ([BRINGUP][bringup], steps 3 and 10) | done: [#77] | C + D held at power-on first make the safe start (the badge skips the boot item, [#1]). After 3 s the arrow runs and the boot ROM starts. |
| Hold A + B for 3 s: the bring-up test ([USAGE][u-bringup]) | partly: [#59] | The same two switches now only leave the factory test. See [Bring-up test](#bring-up-test). |
| Tap A on the name tag or an animation: brightness ([USAGE][u-nametag]) | replaced | Outside apps, B is brighter and D dimmer. The level shows on the grid for a second. |
| ◀ / ▶ on the name tag or an animation: previous / next screen ([USAGE][u-nametag]) | partly: `ledmatrix.c` | A tap of C shows the next: the name tag, then every animation and text in store order, then the name tag again. Apps are skipped: an app keeps C, so browsing into one would end the browsing. The old animations are apps now, and the recommended set holds only apps, so on a new badge C has nothing to browse until people store their own animations or texts. Only forward: B and D stay the brightness, and C + D is a chord. NAME TAG in the menu leaves an app, and the next tap of C starts the first animation or text. |

## Menu and settings

| Old feature | Status now | Notes |
|---|---|---|
| The menu: label on the grid, place on the outline, group on the arms, 20 s timeout ([USAGE][u-menu]) | done: [#34] | Groups now: orange the name tag, purple animations, blue texts, green apps, white settings. C / D step; the new BUTTONS setting makes it B / D. |
| Fixed entries: NAME, HACKER HOTEL, 6 animations, 11 games, VICTORY, EDIT NAME, LIGHT, SPONSORS, SLEEP | replaced: [#1], [#34] | The menu lists the items in the store. The animations and games are apps in `apps/`. A new badge gets the recommended set (`apps/recommended.txt`, **Erase badge and install recommended apps** in the web interface's App library). It holds 3 of the 6 old animations and 6 of the 11 old games. The rest are in the web interface's App library. For VICTORY and SPONSORS, see [Challenges](#challenges-and-the-victory-dance) and [Sponsor reel](#sponsor-reel-charging-and-standby). |
| LIGHT: 5 levels (night, 5 %, 12.5 %, 31 %, full), a bar on the grid, 12.5 % at first start ([USAGE][u-menu]) | done: [#34], [#48], `menu.c` | BRIGHTNESS has 6 levels (8, 16, 32, 64, 128, 255). Six dots under the label show the level; B / D outside apps show them for a second. A new badge starts at 32 (12.5 %). |
| SLEEP: dark until a button; that button only wakes; USB disconnects | done: [#34], `standby.c` | The same behaviour. The standby current is not measured: [#14]. |
| New in this firmware | done | BUTTONS ([#34]), UPDATE ([#34]), safe start ([#1]). |

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
| Name swap: B on the name tag sends the name; the other badge says HI and sends its name back ([USAGE][u-ir]) | replaced: [#37] | The NAMETAG app (`apps/nametag`, the recommended set's boot item) swaps names by itself, and C says hello at once. The firmware's SAY HI is gone, to save 1 KB of flash: outside apps a badge answers no HELLO. The app still swaps names with badges whose firmware has SAY HI. Badge friends in the app: [#72]. |
| A greeting that arrives while the menu, the editor or a game is open waits, and shows after | partly: [#37] | Only the NAMETAG app greets. Another app gets the packets; outside apps the firmware drops them. |
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
| Set the brightness | done | USB command `0x32` BRIGHTNESS (1 .. 255, kept over power-off): **Brightness** in the web interface's **Your badge** (the 6 levels), and `tools/badge.py brightness N`. |
| Read the state every second: what shows, the brightness, charging | partly | The page asks every second: mode, brightness, store, app API. There is no charging bit ([#9]). |
| Windows: Microsoft OS 2.0 descriptor, WinUSB, no driver to install ([README][r-setup]) | done: [#77] | USB 2.1 with a BOS descriptor: the Microsoft OS 2.0 set (WINUSB and a DeviceInterfaceGUIDs property) and the WebUSB landing page. `tools/usb_test` checks every length. Not yet checked on Windows. |
| USB serial number made from the chip's unique ID ([BRINGUP][bringup], step 2) | done: [#77] | 16 hex digits, from the first 8 bytes of the unique ID. `tools/badge.py list` and the web interface show it, and `tools/badge.py --serial` picks a badge. |
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
| `level N` | replaced | `tools/badge.py brightness N` (1 .. 255), and the web interface. |
| `hello` | replaced: [#37] | The NAMETAG app: tap C. No USB command. |
| `test` | partly: [#11] | `tools/badge.py bringup` walks the LEDs from the host. `docs/BRINGUP.md` has the rest as a checklist. |
| `factory` | replaced: [#59] | `tools/badge.py factory`: it stores the factory test app and starts it. |
| `factory skip` | untracked | |
| `sponsors` | tracked: [#12] | |
| `progress` | tracked: [#6] | |
| The log: boot, names saved, IR packets | replaced | Over SWIO. `tools/badge.py irlog` shows the raw IR ([#53]). |

## Factory test

The old test is in [USAGE][u-factory] and [`src/factory.rs`][factory].
This one is in `docs/FACTORY_TEST.md`.

| Old feature | Status now | Notes |
|---|---|---|
| Runs at every power-on until it passes once; kept in flash | done: [#59] | It runs at power-on while the factory test app is stored and the test has not passed; `factory.bin` or `tools/badge.py factory` stores the app. The emulator does the same, and `--fresh` stores the app (`tools/emu/emu.py`). |
| LED and ROW: the volunteer taps B (good) or A (bad) | replaced: [#62] | Both run by themselves. A bad LED or row means the repair pile. |
| KEY, fails after 20 s | done: [#59] | `make factory-image FACTORY_KEYS=0` leaves it out ([#62]). |
| USB, fails after 10 s | done: [#59] | Also shows `CHG` while the battery charges. |
| IR, then `IR?` after 10 s; B passes it anyway, A fails it | partly: [#62] | `IR?` after 10 s, a fail after 30 s. There is no way to pass IR untested. |
| A failed step: tap B to try it again | done: [#59] | |
| Hold A + B for 3 s: on to the bring-up test | partly: [#59] | Leaves the test without a pass. It runs again at the next power-on. |
| The log on the UART, with hints for the volunteer | replaced: [#59] | Over SWIO, for example `factory FAIL KEY: missing: . . C D`. |
| The card for the flashing table ([USAGE][u-card]) | done: [#59] | In `docs/FACTORY_TEST.md`. |

## Bring-up test

The old bring-up test ([USAGE][u-bringup], [`src/selftest.rs`][selftest])
checked a new board step by step, with a log on J3. Issue [#11] tracks it.
The factory test now covers some of the steps, `tools/badge.py bringup`
walks the LEDs from the host, and `docs/BRINGUP.md` is the checklist for
a new board.

| Old step | Status now | Notes |
|---|---|---|
| COM: all 24 PWM channels on one COM line, PB0..PB15 in turn | done: [#59], `tools/badge.py` | The factory ROW step walks the rows and columns in the order you see. `tools/badge.py bringup --lines rows` walks them in wire order, with the designators on each line. |
| PWM: one PWM channel on all COM lines, PA0..PA23 in turn | done: [#59], `tools/badge.py` | The same, with `--lines cols`. |
| LED: one LED at a time, with its designator in the log (`slot 41: PB1 PA17 D35 green`) | done: `tools/badge.py` | `tools/badge.py bringup` lights one LED at a time over USB and prints its designator from `webui/board_data.js`, its colour, COM line, PWM channel and column resistor. No firmware bytes. `docs/BRINGUP.md`, step 9. |
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
| Charging screen: a battery that fills, over and over, while USB charges; a tap hides it; not over a game or the menu ([USAGE][u-always]) | done in the NAMETAG app: [#9] | The firmware debounces CHRG (PB22, `power.c`) for the factory test and apps' `charging()`. The NAMETAG app, the boot item of the recommended set, shows the battery; a tap hides it. The firmware's own name tag, games and the menu show none. |
| Standby until a button; the button only wakes | done: `standby.c`, [#34] | |
| Unused pads pulled up, so they draw no current ([README][r-hw]) | untracked | No code in this firmware pulls up the unbonded or unconnected pads. This can matter for [#14]. |

## Emulator and tools

| Old feature | Status now | Notes |
|---|---|---|
| The firmware on the PC, the buttons drawn on the board ([USAGE][u-emu]) | done: [#3] | Apps run in a RISC-V interpreter. Keys A, B, C, D, or click. |
| `C`: USB power on and off (charging) | done: `tools/emu` | `V` plugs USB power in and out (PC3). `U` makes the battery charge, stop, or go away: then CHRG (PB22) toggles, as the charger does without a battery. Buttons under the board do the same, and `tools/emu/headless.py` has `usb_power()`, `charging()` and `no_battery()`. |
| `R`: reset | done: [#3] | `R` powers off and on; a key held then is held at power-on (safe start). |
| A name kept over `R`, lost when the emulator closes | done: [#3] | The flash is a file, so everything stays. |
| `--fresh`: the factory test, then the sponsor reel | partly: `tools/emu` | `--fresh` erases the persistent record and stores the factory test app (the items stay), so the factory test runs at power-on. It runs on a copy of the flash file; `--keep` changes the file itself. There is no sponsor reel yet ([#12]). |
| One shared infrared room (UDP 127.255.255.255:8028) | done: [#3], [#37] | |
| stdin is the UART console | tracked: [#10] | stdin carries the emulator's own protocol now. |
| `--web`: the setup page on localhost, any browser | done: [#3] | The whole web interface. |
| `parity.py`: C and Rust frame by frame | replaced | One firmware now. |
| `ir_game.py`: 4 in a row on two emulators, also with 30 % of packets lost | partly: [#38] | `tools/four_test` plays whole games on two emulators, without lost packets. |
| USB protocol fuzz test, 200 000-frame soak | partly | `tools/app_test` gives every app random buttons in the emulator. No USB fuzz test. |
| `tools/sim/docs_gifs.sh`: GIFs of every screen for the docs | untracked | The docs here have no GIFs. |
| CI fails the build over 61440 bytes, the most the `wchisp` command-line tool can flash without harm there ([README][r-tools]) | untracked | The linker stops at 30720 B (`ledmatrix.ld`). The `wchisp` command-line tool can erase the start of the item store from 29697 B (see [Hardware notes](#hardware-notes)). Nothing checks for that. The firmware is 26448 B now. |
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
  not the datasheet's 55 µA ([#14]). But the IR receiver S1 is always on
  (VCC through R28), and its listing gives up to 2.5 mA: it can account
  for most of that. See `docs/BRINGUP.md`, step 12.

## Untracked gaps

Candidates for new issues. None of them is opened yet. Each needs a
decision first: is it worth the flash, and is it worth the work?

1. **Brightness over USB.** Done: the USB command BRIGHTNESS
   (`webusb_protocol.h`), in the web interface and in `tools/badge.py`,
   as the old setup page and `level N` had it.
2. **Brightness on the badge.** Done: a new badge starts at 12.5 %, as
   the old firmware did, and BRIGHTNESS shows the level on the grid, as
   B / D outside apps do for a second (`menu.c`, `ledmatrix.c`).
3. **Browse without the menu.** Partly done: a tap of C outside apps
   starts the next animation or text, as ◀ / ▶ did. But the old
   animations are apps now, and the recommended set holds only apps, so
   on a new badge C has nothing to browse. C skips apps: an app keeps
   C, so a tap that started one would end the browsing (the NAMETAG app
   that the recommended set starts at power-on looks like the name tag,
   so it would look as if C stopped working). Apps start from the menu.
   NAME TAG in the menu leaves an app, and the next tap of C starts the
   first animation or text.
4. **A boot ROM chord.** Done ([#77]): C + D for 3 s reboots into the boot ROM
   (`ledmatrix.c`), so a badge with a broken menu can still be flashed
   over USB.
5. **WinUSB on Windows.** Done ([#77]): a BOS descriptor with the Microsoft OS
   2.0 descriptor (`usb_descriptors.h`), so Windows needs no driver
   install for the badge. The boot ROM still needs one.
6. **A unique USB serial number.** Done ([#77]): from the chip's unique ID
   (`usb_descriptors.h`), so two badges on one PC can be told apart.
7. **Pull up the unused pads** (PC6–PC9, PC12, PC13, PC20, and PC0–PC2,
   PC14, PC15). Could be part of [#14].
8. **Factory test overrides.** A way to pass IR untested, as B on `IR?`
   did, and to mark the test as passed (`factory skip`). This is a
   decision for the organisers of the flashing table.
9. **Emulator: USB and charging.** Done (`tools/emu`): `V` turns USB
   power on and off, and `U` makes the battery charge, stop, or go away
   (CHRG then toggles), for [#9] and the factory test's USB step.
10. **Emulator: a fresh start.** Done (`tools/emu`): `--fresh` runs the
    factory test at power-on, on a copy of the flash file with the
    persistent record erased.
11. **Bring-up from the host.** Done: `tools/badge.py bringup` walks
    the LEDs one at a time over USB and prints each designator from
    `webui/board_data.js`, with its COM line, PWM channel and column
    resistor. `--lines` lights whole COM lines or PWM channels. It needs
    no firmware bytes.
12. **A bring-up checklist** for this firmware, as the old BRINGUP.md
    was. Done: `docs/BRINGUP.md`, from visual inspection to the standby
    current, with the parts and nets from the KiCad files.
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
[#72]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/72
[#77]: https://github.com/badgeteam/hackerhotel2027-firmware/issues/77
