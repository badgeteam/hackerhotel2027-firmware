# Apps

An app is a small C program that runs on the badge next to the stored
animations. You build it into a `.hhapp` file and add it with the web
interface ("On the badge", **Add app or animation**) or with
`tools/badge.py add`.

`hhapp.h` is the whole interface between the firmware and an app. Read it
first.

## Build

```sh
cd apps
make                  # every apps/<name>/ into build/<name>.hhapp
../tools/badge.py add build/snake.hhapp
```

You need the same `riscv64-elf-gcc` as the firmware. For `tools/badge.py`
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
- **Buttons.** `pressed()` gives the buttons that went down since the last
  tick, and `held()` gives the buttons that are down now. The bits are
  `HH_BTN_A`..`HH_BTN_D`, by the silkscreen letters.
- **Leave the app.** Hold A and C for one second. The badge also leaves
  the app when the web interface sends a command (a frame, Stop, Run), or
  when the app calls `exit()`.
- **Safe start.** Hold any button while the badge powers on, and it does
  not start the boot item.

## Limits

- **Size.** The image and its `.bss` together must fit in 12 KB of RAM
  (`HH_APP_RAM_SIZE`). The store holds 31.75 KB for all items together.
- **No C library.** `sdk.c` supplies `memset` and `memcpy`. Do not use
  64-bit division: the toolchain's libgcc is 64-bit only.
- **Stack.** The app runs on the firmware's stack, about 5 KB. Keep big
  arrays `static`.
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
