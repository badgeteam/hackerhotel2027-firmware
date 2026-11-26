# Hacker Hotel 2027 starfish badge firmware

[![Tests](https://github.com/badgeteam/hackerhotel2027-firmware/actions/workflows/test.yml/badge.svg?branch=main)](https://github.com/badgeteam/hackerhotel2027-firmware/actions/workflows/test.yml)

This is the firmware for the Hacker Hotel 2027 starfish badge, a WCH
CH643Q with 384 LED channels. It is written in C, on
[ch32fun](https://github.com/cnlohr/ch32fun).

You control the badge from a web interface over WebUSB (`webui/`). From
the web interface you can:

- draw on the LEDs,
- make animations from video, animated GIF (or WebP, PNG) or text,
- store animations and apps on the badge,
- set your name.

When nothing else runs, the badge shows its **name tag**: your name on the
centre grid, or "HACKER HOTEL 2027" while no name is set. Shoot it with a
Hacker Hotel 2025 banana and a ring goes out, with the arms blinking in
the shooter's team colour; a heal pulses them green.

## Flash and RAM

The CH643 has **62 KB of flash and 20 KB of RAM**. That is small for a
badge with 384 LEDs, a web interface and apps, so every feature has to
earn its place. This is how the space is split, and what uses it now:

| Memory | Size | Use | In use now |
|---|---|---|---|
| Flash | 30 KB | firmware | 27.6 KB |
| Flash | 31 KB | item store: animations, texts and apps | what you store |
| Flash | 1 KB | persistent record: name, brightness, boot item, app data | 2 × 512 B |
| RAM | 8 KB | firmware data and stack | 4.7 KB data, the rest is stack |
| RAM | 12 KB | the app that runs (its code and data); while an animation plays, the animation decoder | snake: 1.4 KB code + 0.6 KB data; the decoder 3.5 KB |

How we juggle space and usability:

- **The firmware does only what every badge needs, or what must be
  fast:**
  - the LED driver, USB, infrared, the buttons and standby;
  - the item store and the persistent record;
  - the animation player and the app host;
  - the font, the name tag and the menu.

  Everything else is an item in the store. You only pay space for what
  you put on your badge.
- **Shared things live in the firmware, once, so apps stay small.** The
  font is in the firmware and every app can use it, instead of every app
  bringing its own
  ([#5](https://github.com/badgeteam/hackerhotel2027-firmware/issues/5)). The LED layout tables reach apps the same way. A UART for
  apps is proposed in [#22](https://github.com/badgeteam/hackerhotel2027-firmware/issues/22) and [#10](https://github.com/badgeteam/hackerhotel2027-firmware/issues/10).
- **Apps are native code, copied into RAM to run.** That makes them
  small: sparkle is 0.6 KB and snake 1.4 KB. So many fit in the store, and
  the old firmware's games and animations can come back as apps
  ([#17](https://github.com/badgeteam/hackerhotel2027-firmware/issues/17), [#18](https://github.com/badgeteam/hackerhotel2027-firmware/issues/18)). The price: one app runs at a time, in at
  most 12 KB, code and data together.
- **Animations are the big users of the store.** The web interface
  compresses them (LA04, `docs/ANIMATION_FORMAT.md`) and shows the size
  before you store one. LA04 makes them two to four times smaller than
  its predecessor (video about 1.3 times). For that the firmware spends
  3 KB more flash on the decoder, and the decoder borrows the app RAM
  while an animation plays, because no app runs then. Fewer brightness
  levels, fewer frames or a lower frame rate make an animation smaller.
- **The store works in 256-byte flash pages.** Every item takes whole
  pages, plus a 32-byte header. The web interface shows what each item
  takes.
- **The persistent record is small and written rarely.**
  - It holds 28 bytes per app, for high scores and settings.
  - It is written 1 s after the last change, not on every change, because
    every flash write wears the flash.
  - It is kept twice, so a power loss leaves the previous copy.

## Buttons

The letters are the ones on the silkscreen: A top left, B top right,
C bottom left, D bottom right.

| Button | |
|---|---|
| **Hold A** | the menu, also from an app (hold it for a second there) |
| **Tap C** | the next item (when no app runs): see below |
| **B** / **D** | brighter / dimmer (when no app runs) |

When no app runs and the menu is closed:

- **B** and **D** show the brightness level for a second at the bottom of
  the grid, over what runs: six dots, one for each level, bright up to
  the level now.
- **A tap of C** starts the next item, in the menu's order. After the
  last one comes the name tag, and after the name tag the first stored
  animation, text or app. On the name tag, C goes on after the item that
  ran last. A tap is shorter than half a second, and acts when you let
  go of C. C with D down at any moment of the press is no tap.
- **An app gets C itself**, so C does not browse while an app runs. That
  includes the app a badge starts at power-on (the recommended set starts
  the NAMETAG app). To go on from an app, hold A for the menu and pick
  NAME TAG: a tap of C then starts the item after the app.
- Only C steps through the items, and only forward: B and D stay the
  brightness. C goes forward even though it is "previous" in the menu:
  with one button, forward is the way round the ring. The menu's BUTTONS
  setting changes only the menu and the name editor, not this.

**The menu** lists the name tag, SAY HI, every stored animation, text and
app, and the badge's settings. The name of the entry shows on the grid,
the outline shows where you are in the list, and the arms show the group:
orange the name tag and SAY HI, purple animations, blue texts, green apps,
white settings.

| In the menu | |
|---|---|
| **C** / **D** | previous / next entry |
| **Tap A** | start it (or change the setting) |
| **Hold A** | close the menu, and go on with what ran before |
| no button for 20 s | the menu closes |

**SAY HI** swaps names with another badge over infrared: see
[Meeting other badges](#meeting-other-badges).

The settings in the menu:

- **EDIT NAME:** type your name on the badge.
  - C / D: previous / next character (hold to keep going).
  - Tap A: next position. One blank after the end, it goes back to the
    first character.
  - B: previous position.
  - Hold A: save, and show the name tag.

  The characters come in this order: blank, A–Z, a–z, 0–9, then signs.
  To make the name shorter, set the characters at the end to blank. After
  a minute without a button, the editor closes without saving.
- **BRIGHTNESS:** each tap is one level brighter, then back to the
  dimmest. Six dots under the label show the level. A new badge starts
  at the third level (32 of 255, 12.5 %).
- **BUTTONS:** which pair steps through the menu. `C D` is the bottom pair,
  side by side (the default). `B D` is the right pair, one above the
  other: then B is previous, and in the editor C is the previous
  position.
- **SLEEP:** standby until you press a button. USB disconnects while the
  badge sleeps.
- **UPDATE:** reboot into the USB boot ROM, to flash new firmware.

Further:

- **Apps.** While an app runs, the app gets all four buttons. Holding A
  for a second opens the menu, and the app waits. The app also gets that
  press of A. Close the menu (hold A) and the app goes on; choose another
  entry and the app stops.
- **Safe start.** Hold any button while the badge powers on. The badge
  then does not start the boot item, so an app that hangs cannot lock up
  the badge.
- **Brightness.** The badge keeps the brightness when it is powered off.

## Build

You need `riscv64-elf-gcc`. The ch32fun tree in `ch32fun/` contains the
rest.

```sh
make -C ch32fun/minichlink                    # once: the flash tool
make MINICHLINK=ch32fun/minichlink ledmatrix.bin
```

The Makefile looks for `minichlink` with `which`. If minichlink is on
your `PATH`, plain `make` builds and flashes in one step.

## Flash over USB

The CH643's boot ROM speaks USB ISP. It shows up as `1a86:55e0` and
waits about 5 s for a host. Do the steps in this order:

1. Start the flash first, so that it waits for the boot ROM:

   ```sh
   until lsusb -d 1a86:55e0 >/dev/null; do sleep 0.1; done
   ch32fun/minichlink/minichlink -w ledmatrix.bin flash -b
   ```

2. On the badge, open the menu (hold **A**) and choose **UPDATE**.

Flashing the firmware does not erase the stored items, the name or the
other settings: minichlink writes only the firmware's own pages.

On Linux, give your user access to the boot ROM and to the badge with a
udev rule, for example in `/etc/udev/rules.d/99-hh27-starfish.rules`:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="d643", MODE="0666"
```

Then run `sudo udevadm control --reload` and plug the badge in again.

## Flash with a WCH-Link probe

With a WCH-LinkE (or another WCH-Link) on the badge's SWD pads, you can
flash with [wlink](https://github.com/ch32-rs/wlink)
(`cargo install wlink`). This also works on a badge whose firmware does
not start, or that has no firmware yet, where the USB way above cannot.

1. Put the probe in RISC-V mode. `lsusb` must show
   `1a86:8010 QinHeng Electronics WCH-Link`. If it shows `1a86:8012`, the
   probe is in DAP mode; switch it once with:

   ```sh
   wlink mode-switch --rv
   ```

   `1a86:55e0` is not the probe: that is the CH643's own USB boot ROM,
   used by the USB way above.
2. Connect GND, SWDIO and 3V3. If the probe's 3V3 is not enough for the
   badge, power the badge from USB as well.
3. Flash:

   ```sh
   wlink --speed low --chip CH643 reset   # must print "Attached chip: CH643"
   wlink --speed low --chip CH643 flash --address 0x08000000 ledmatrix.bin
   ```

   - `--speed low` is needed: at the default speed the probe gives
     `WCH-Link underlying protocol error: 0x55`.
   - Reset first, otherwise the flash can fail.
   - The same `0x55` error also means the chip did not answer: no
     contact on a pin, or no power.

`wlink flash` does not erase the chip first. `wlink erase` (or
`wlink flash --erase`) erases all of it: the stored items, the name and
the settings too.

## Web interface

WebUSB needs Chrome, Edge or Opera, and a secure context: localhost is
sufficient.

```sh
cd webui && python3 -m http.server 8027 --bind 127.0.0.1
```

Open http://127.0.0.1:8027/ and click **Connect badge**. In the
browser's box, pick **Hackerhotel 2027 starfish**. The **Your badge**
panel then shows the badge's state and lets you set your name
(**Save name**) and the brightness (**Brightness**, the six levels of the
badge's menu; `tools/badge.py brightness N` sets any value from 1 to
255). The **On your badge** panel shows what is stored on the badge. In
that panel you can:

- start an item (**Start**),
- choose the item that starts at power-on (the star button),
- delete an item (the trash button),
- add a `.hhapp`, a `.ledanim` or a `.txt` (**Add from file…**),
- type a text and add it (**Show a text**, **Add text to badge**),
- stop what plays (**Stop playing**),
- erase the store (**Erase the badge**, **Erase all items**).

The **App library** panel adds or updates the apps in `apps/`, built into
`webui/apps/` by `make -C apps webui`.

In **Create an animation**, **Save to badge and play** stores the
animation you made.

With **Show on badge while drawing** on, the badge shows what you draw
and the frames of **▶ Play**. If the badge cannot keep up with the speed,
it skips frames. **Start**, **Stop playing** and an upload in the
**On your badge** panel stop **▶ Play**. The badge then runs that item
until you draw again.

To put the web interface on a web server (HTTPS; WebUSB needs a secure
context), use `tools/deploy_webui.sh HOST:PATH` (a dry run; add `--go`).
It deploys the committed `webui/` of the current commit, and the page
shows that commit in its footer. When a badge's firmware is older than
the page expects, the **Your badge** panel says so and shows an
**Update firmware** button that takes you to the **Firmware** panel.

The **Firmware** panel flashes new firmware. **Update firmware to
VERSION** flashes the firmware built from the page's own commit;
**Advanced: flash a .bin file** flashes a `.bin` you choose. Click
**Flash now**, then pick "Unknown device from QinHeng Electronics" in the
browser's box. The items, the name and the settings stay.
`docs/FIRMWARE_UPDATE.md` describes how it works. Served from this
checkout, the panel flashes what `make webui-firmware` copied to
`webui/firmware/`.

Only one program at a time can have the badge open. Click
**Disconnect** before you use `tools/badge.py`.

## Emulator

The emulator runs the firmware on a PC: the main loop, the item store, the
persistent record, the animation player and the app host, built from the
same C files. Apps run in a small RISC-V interpreter, so the emulator runs
the same `.hhapp` files as the badge.

```sh
make -C tools/emu
python3 tools/emu/emu.py --web
```

The window shows the badge. Use these keys:

| Key | |
|---|---|
| **A**, **B**, **C**, **D** | the buttons (you can also click them) |
| **R** | power off and on; a button held then is held at power-on, for a safe start |
| **Esc** | quit |

- **Flash.** The emulated flash is a file, `~/.cache/hh27-emu/flash.bin`
  by default (`--flash` to change). Your items, name and settings stay
  there after **R** and after you close the emulator.
- **Web interface.** With `--web`, the emulator serves the web interface on
  http://127.0.0.1:8027/ (`--port` to change). There, click **Connect
  emulator**. This works in every browser, not only in the ones with
  WebUSB.
- **Infrared.** All emulators on one PC share an infrared "room", so apps
  on two emulators can reach each other.
- **Log.** The firmware's log appears in the terminal.

`tools/emu/rv32_test/run.sh` checks the interpreter against the same
code built for the PC.

## Meeting other badges

Hold your badge so that its infrared LED and receiver face the other
badge, close by. Open the menu (hold **A**), press **D** once to reach
**SAY HI**, and tap **A**.

- Your badge shows `HI?` and sends your name.
- The other badge answers with its name, whatever it shows, as long as no
  app runs there. Both badges show `HI` and the other's name, scrolling
  through once.
- Without an answer, your badge tries 4 times in 6 seconds, then shows
  `NO REPLY`.
- **B**, **C** or **D** ends the greeting early. What ran before then
  goes on.

**4 in a row** (`apps/four`, in the web interface's App library) plays
against another badge over infrared. Start it on both badges and point
them at each other: they pair up by themselves, and the badge that asked
first starts. C / D aim, B drops. Drop a disc before another badge
answers, and you play against your own badge. After 20 s without an
answer from the other badge, the game ends.

An app gets the infrared to itself: a badge that runs an app does not
answer a SAY HI. The packets (`ir_codec.h`) can carry up to 32 bytes, and apps can
send and receive them too (`apps/README.md`).

## What the badge stores

The top 32 KB of flash is the item store (`store.h`):

- animations, texts and apps, one after the other;
- in the last 1 KB, a persistent record (`persist.h`) with the boot item,
  your name, the brightness, and 28 bytes of data for each app (high
  scores and the like).

The record is kept twice, so a power loss while it is written leaves the
previous copy. **Erase all items** removes the items but keeps the record.

A **text** is stored as its characters, one byte each, so the store holds
about 31 000 of them. The badge shows a text as the name tag shows your
name: still if it fits, else scrolling. Add one in the web interface (**On
your badge**, **Show a text**, or a `.txt` file) or with `tools/badge.py add
FILE.txt`. The badge shows printable ASCII: the web interface drops
accents, and shows other characters as `?`. The format is in `nametag.h`.

When you delete an item, the items after it move down to fill the gap.
Keep the badge powered until the delete is done: a power loss during
the move can lose the items after the deleted one.

In flash, the firmware is at 0 .. 30 KB, the items at 30 .. 61 KB, and
the persistent record at 61 .. 62 KB. In RAM, the firmware is at
0 .. 8 KB, and the running app at 8 .. 20 KB (see
[Flash and RAM](#flash-and-ram)).

## More

| | |
|---|---|
| `apps/README.md` | write and build an app (`apps/hhapp.h` is the interface) |
| `docs/ANIMATION_FORMAT.md` | the `.ledanim` animation format, LA04 |
| `docs/LEDPWM_MATRIX.md` | how the LEDs are wired, and the slot numbers |
| `docs/FIRMWARE_UPDATE.md` | the firmware update from the web interface, and the boot ROM's protocol |
| `docs/OLD_FIRMWARE.md` | what the old firmware had, feature by feature, and where each feature stands now |
| `webusb_protocol.h` | the USB protocol |
| `tools/badge.py` | the item store, the name, run and stop, from the command line (needs pyusb) |
| `tools/deploy_webui.sh` | publishes `webui/` of the current commit to a web server |
| `tools/extract_board_data.py` | makes `webui/board_data.js` from the KiCad board in the hardware repository |
| `tools/gen_layout.py` | makes `layout.c`, `la04_tables.h` and `webui/layout_data.js` from `webui/board_data.js` |
| `tools/codec_test/run.sh` | checks that the web interface and the firmware decode animations the same way |
| `tools/app_test/run.sh` | checks the apps: game rules on the PC (`mock_hh.h` stands in for the firmware), that no app draws over its own text in the same tick (`overdraw_run.c`, `MOCK_OVERDRAW=all` lists the octagon's clipped pixels too; a text read back with `get()` and then cleared is a mask, as in the name tag, and not checked, nor is what the app draws over that pixel later in the tick), then every app in the emulator with random buttons |
| `tools/text_test/run.sh` | checks text items: the web interface's payloads, played on the emulator |
| `tools/four_test/run.sh` | checks 4 in a row: its rules on the PC, and whole games on two emulators |
| `tools/ir_test/run.sh` | checks the infrared codec with a model of the receiver, and the name swap on two emulators |
| `tools/board_test/run.sh` | checks that `tools/extract_board_data.py` makes the committed `webui/board_data.js` from the board (SKIP without it) |
| `tools/nametag_test/run.sh` | checks how the name tag shows banana shots, on the emulator |
| `tools/nametag_app_test/run.sh` | checks the NAMETAG app (`apps/nametag`): badges that meet, the panel, a power cycle, a plain badge, banana shots, on emulators |
| `tools/brightness_test/run.sh` | checks the brightness on the emulator: the start value, the USB command and `tools/badge.py brightness`, the level on the grid |
| `tools/browse_test/run.sh` | checks that a tap of C steps through the items on the emulator (past an app, left through the menu), and that long presses and C + D do not |
| `tools/emu/headless.py` | the emulator without a window, for tests: USB, buttons, infrared, the text on the grid |
| `tools/isp_test/isp_test.js` | tests the web interface's firmware update offline (`node tools/isp_test/isp_test.js`) |
| `tools/emu/` | the emulator (see above); `hw.c` stands in for the hardware, `rv32.c` runs the apps |

## Contributing

`CONTRIBUTING.md` explains how to build, test and send a change.
`SECURITY.md` explains how to report a security problem privately.

## License

MIT, see `LICENSE`. Copyright (c) 2026 the badge.team contributors.

`ch32fun/` is a copy of [ch32fun](https://github.com/cnlohr/ch32fun) and
keeps its own MIT license, in `ch32fun/LICENSE`.
