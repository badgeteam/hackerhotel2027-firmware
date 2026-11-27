# Apps

An app is a small C program that runs on the badge next to the stored
animations. You build it into a `.hhapp` file and add it with the web
interface ("On the badge", **Add app or animation**) or with
`tools/badge.py add`.

`hhapp.h` is the whole interface between the firmware and an app.
[docs/APP_API.md](../docs/APP_API.md) explains every member, with
examples.

## Build

```sh
cd apps
make                  # every apps/<name>/ into build/<name>.hhapp
../tools/badge.py add build/snake.hhapp
make webui            # also into the web interface's app library (webui/apps/)
```

The web interface's **App library** offers every app in `webui/apps/`
with one click. `make webui` copies the built apps there and writes
`webui/apps/index.json`, with each app's description taken from the
comment at the top of its first `.c` file. Commit `webui/apps/` after a
change to an app, so the site serves the new build.
`apps/factory`, the factory test, is not in the library: `make
factory-image` and `tools/badge.py factory` store it
(`docs/FACTORY_TEST.md`).

You need the same `riscv64-elf-gcc` as the firmware. Without a badge, try
your app in the emulator: `python3 tools/emu/emu.py --web`, then add the
`.hhapp` in the web interface. A fault (a bad pointer, a jump outside the
app) or a `tick()` that does not return stops the app there, with a
message in the terminal. For `tools/badge.py`
you also need pyusb (`pip install pyusb`).

## Write one

Make a directory `apps/<name>/` and put one or more `.c` files in it:

```c
#include "hhapp.h"

static const hh_api_t* hh;

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    if (hh->pressed() & HH_BTN_B) hh->set(hh->grid[8][8], 255);
}

HH_APP(init, tick);
```

The firmware calls `init()` once and then `tick()` about every 5 ms.
`tick()` must return quickly.

- **LEDs.** Each LED has a slot (`row * 24 + col`) in the matrix. The
  layout tables give the slots by position:
  - `grid[y][x]`: the centre grid, 17 × 16, with `HH_NONE` in the corners.
  - `edge[]`: the outline, clockwise from the top.
  - `rgb[]`: the RGB LEDs on the arms, by arm and rank.
- **Positions.** `polar[slot]` gives each LED's place: distance from the
  grid's centre in 1/4 mm, and angle in 1/256 turn, clockwise from the top
  (API version 3). The animations (ripple, galaxy, ...) use it for round
  effects. `fx.h` has their helpers: `fx_set()` (which takes linear light,
  as the previous firmware's animations did), and B / D for the
  brightness.
- **Helpers.** From API version 4 the firmware has what many apps need:
  `set_rgb()`, `sin8()`, `hsv()`, `isqrt()`, `atan8()`, `draw_number()`,
  `step_brightness()` and `set_linear()`. Use them rather than a copy in
  the app: the app stays small, and the store has room for more.
- **Text.** `draw_text(x, y, text, value)` writes text on the centre
  grid, with its top left corner at column `x`, row `y`, in the
  firmware's 5 × 8 font (7 rows, plus one for descenders), which has all
  of printable ASCII. `x` and `y` may be off the grid, which makes
  scrolling easy. It returns the width; `text_width(text)` gives the same
  without drawing. Each character takes 6 columns (`HH_FONT_ADVANCE`). The grid is 17 wide, so about 3
  characters fit at a time. Snake uses it to show the score. Draw the
  text last, or leave the grid around it dark: anything set on the grid
  after it in the same tick (walls, effects) cuts pixels out of it.
  `tools/app_test/run.sh` fails when an app does that.
- **Infrared.** `ir_send()` and `ir_receive()` send and get NEC frames,
  as a remote control sends. `ir_send_packet(type, data, length)` and
  `ir_receive_packet()` send and get packets from badge to badge: a type
  byte and up to 32 data bytes, with a CRC on air. A packet with 10 data
  bytes takes 0.2 s. Nothing confirms that a packet arrived: let the
  other badge answer, and send again if no answer comes. Types `'H'` and
  `'R'` are the name swap (`apps/nametag`, and older firmware's SAY HI):
  while an app runs, it gets those too. In the
  emulator, the badges on one PC share the infrared.
- **Buttons.** `pressed()` gives the buttons that went down since the last
  tick, and `held()` gives the buttons that are down now. The bits are
  `HH_BTN_A`..`HH_BTN_D`, by the silkscreen letters.
- **Leave the app.** Hold A for one second: the badge's menu opens, and
  the app waits (no ticks) until the menu closes again; choosing another
  entry in the menu stops the app. The app gets that press of A too, so
  do not make a long press of A do something. The badge also leaves
  the app when the web interface sends a command (a frame, Stop, Run), or
  when the app calls `exit()`. After an app, the badge shows the name tag.
- **The boot ROM.** Holding C and D together for 3 s reboots the badge
  into its USB boot ROM, also from an app. Do not make people hold both
  for that long.
- **Saving.** `name()` gives the owner's name, which is set in the web
  interface or by an app with `set_name()`. An app can keep up to 28
  bytes with `save()` and `load()`, for example its high scores. The data
  stays when the badge powers off and when the store is erased. It is
  kept by the app's name in the store, so the same app gets it back after
  an upload. Save only when something changed: each save is a flash write.
- **Brightness.** `set_brightness()` sets the global brightness, the same
  one that B and D set outside an app. The badge keeps it when it is
  powered off.
- **Charging.** `charging()` is true while the battery charges (API
  version 8). Outside apps the badge shows a charging screen then, but not
  over an app: an app that wants to show it draws its own.
- **Gamma.** LED values are brightness as a screen shows it. The firmware
  applies gamma 2.2, so 128 looks half as bright.
- **Safe start.** Hold any button while the badge powers on, and it does
  not start the boot item.
- **Demo mode.** The games open with a demo: the badge plays by itself
  until the first button (B, C or D; A is the menu's), which starts a real
  game. 15 s after a game without a button, the demo comes back. A demo
  never saves a score. The demo players are the games' own, and the tests
  in `tools/app_test` use them too.

## Limits

- **API version.** An app does not start on firmware with an older
  `HH_API_VERSION` than the one it was built against (9: `usb_power`,
  `ir_loopback`, `factory_passed`; 8: `charging`;
  7: raw infrared, `ir_send_raw`; 6: shots, `ir_receive_shot`; 5: the infrared packets; 4: the helpers; 3: `polar`;
  2: the text calls); update the
  badge first. `tools/badge.py list` prints the firmware's version ("app API").

- **Size.** The image and its `.bss` together must fit in 12 KB of RAM
  (`HH_APP_RAM_SIZE`). The store holds 31 KB for all items together, each
  rounded up to whole 256-byte pages.
- **No C library.** `sdk.c` supplies `memset` and `memcpy`. Do not use
  64-bit division: the toolchain's libgcc is 64-bit only.
- **Stack.** The app runs on the firmware's stack, about 5 KB. Keep big
  arrays `static`.
- **The name `FACTORY` is taken.** The firmware treats an app stored
  under that name as the factory test (`docs/FACTORY_TEST.md`): it runs
  at every power-on before the boot item, hold A does not open the menu,
  its brightness is not kept, and once the badge has passed the test the
  firmware deletes it. The web interface names an upload after its file,
  so do not call your app `factory.hhapp`.
- **gp.** The app must not use `gp`. The Makefile builds with
  `-msmall-data-limit=0` and links with `--no-relax`, so keep those flags.

## How it works

`app.ld` links the app to run at `HH_APP_RAM_BASE` (0x20002000), with the
`hh_app_header_t` first. `objcopy` then writes the linked image to the
`.hhapp` file.

The firmware stores that file as an item in the top 32 KB of flash
(`store.c`). To start it, `app_host.c`:

1. checks the header,
2. copies the image to RAM,
3. zeroes `.bss`,
4. calls `init()` with the API table.

The firmware itself uses the lower 8 KB of RAM (see the Makefile's
linker script lines).
