# Hacker Hotel 2027 starfish badge firmware

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
centre grid, or "HACKER HOTEL 2027" while no name is set.

## Flash and RAM

The CH643 has **62 KB of flash and 20 KB of RAM**. That is small for a
badge with 384 LEDs, a web interface and apps, so every feature has to
earn its place. This is how the space is split, and what uses it now:

| Memory | Size | Use | In use now |
|---|---|---|---|
| Flash | 30 KB | firmware | 15.7 KB |
| Flash | 31 KB | item store: animations and apps | what you store |
| Flash | 1 KB | persistent record: name, brightness, boot item, app data | 2 × 512 B |
| RAM | 8 KB | firmware data and stack | 3.4 KB data, the rest is stack |
| RAM | 12 KB | the app that runs (its code and data) | snake: 1.4 KB code + 0.6 KB data |

How we juggle space and usability:

- **The firmware does only what every badge needs, or what must be
  fast:**
  - the LED driver, USB, infrared, the buttons and standby;
  - the item store and the persistent record;
  - the animation player and the app host;
  - the font and the name tag.

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
  compresses them (LA03, `docs/ANIMATION_FORMAT.md`) and shows the size
  before you store one. Fewer brightness levels, fewer frames or a lower
  frame rate make an animation smaller.
- **The store works in 256-byte flash pages.** Every item takes whole
  pages, plus a 32-byte header. The web interface shows what each item
  takes.
- **The persistent record is small and written rarely.**
  - It holds 28 bytes per app, for high scores and settings.
  - It is written 1 s after the last change, not on every change, because
    every flash write wears the flash.
  - It is kept twice, so a power loss leaves the previous copy.

## Buttons

The letters are the ones on the silkscreen.

| Button | When no app runs |
|---|---|
| **A** | standby: the badge sleeps until you press a button |
| **B** | brighter |
| **C** | reboot into the USB boot ROM, to flash new firmware |
| **D** | dimmer |

- **Apps.** While an app runs, the app gets all four buttons. To leave
  the app, hold **A + C** for a second; the badge then shows the name
  tag.
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

2. Press **C** on the badge. If an app runs, leave it first with A + C.

Flashing the firmware does not erase the stored items, the name or the
other settings: minichlink writes only the firmware's own pages.

On Linux, give your user access to the boot ROM and to the badge with a
udev rule, for example in `/etc/udev/rules.d/99-hh27-starfish.rules`:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="d643", MODE="0666"
```

Then run `sudo udevadm control --reload` and plug the badge in again.

## Web interface

WebUSB needs Chrome or Edge, and a secure context: localhost is
sufficient.

```sh
cd webui && python3 -m http.server 8027 --bind 127.0.0.1
```

Open http://127.0.0.1:8027/ and click **Connect over WebUSB**. The
**On the badge** panel shows what is stored on the badge. In that panel
you can:

- run an item,
- choose the boot item (★),
- delete an item (🗑),
- add a `.hhapp` or `.ledanim`,
- add or update an app from the **App library**: the apps in `apps/`,
  built into `webui/apps/` by `make -C apps webui`,
- stop what runs,
- erase the store,
- set your name.

**Add to the badge & play on boot** stores the animation you made.

With **Live send** on, the badge shows what you draw and the frames of
**▶ Play**. If the badge cannot keep up with the fps, it skips frames.
**Run**, **Stop** and an upload in the **On the badge** panel stop
**▶ Play**. The badge then runs that item until you draw again.

To put the web interface on a web server (HTTPS; WebUSB needs a secure
context), use `tools/deploy_webui.sh HOST:PATH` (a dry run; add `--go`).
It deploys the committed `webui/` of the current commit, and the page
shows that commit in its footer. When a badge's firmware is older than
the page expects, the page says so next to the badge's status.

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

## What the badge stores

The top 32 KB of flash is the item store (`store.h`):

- animations and apps, one after the other;
- in the last 1 KB, a persistent record (`persist.h`) with the boot item,
  your name, the brightness, and 28 bytes of data for each app (high
  scores and the like).

The record is kept twice, so a power loss while it is written leaves the
previous copy. **Erase all** removes the items but keeps the record.

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
| `docs/ANIMATION_FORMAT.md` | the `.ledanim` format, LA03 and LA02 |
| `docs/LEDPWM_MATRIX.md` | how the LEDs are wired, and the slot numbers |
| `webusb_protocol.h` | the USB protocol |
| `tools/badge.py` | the item store, the name, run and stop, from the command line (needs pyusb) |
| `tools/deploy_webui.sh` | publishes `webui/` of the current commit to a web server |
| `tools/gen_layout.py` | makes `layout.c` and `webui/layout_data.js` from `webui/board_data.js` |
| `tools/codec_test/run.sh` | checks that the web interface and the firmware decode animations the same way |
| `tools/emu/` | the emulator (see above); `hw.c` stands in for the hardware, `rv32.c` runs the apps |
