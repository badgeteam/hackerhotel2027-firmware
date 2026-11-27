# Contributing

Thank you for helping with the Hacker Hotel 2027 starfish badge. This
file says how we work. `README.md` explains the badge, and
`apps/README.md` and `docs/APP_API.md` explain how to write an app.

## How changes get in

`main` is protected:

- Every change goes through a pull request. Nobody pushes to `main`
  directly, and nobody force-pushes or deletes it.
- The CI check **test** (`.github/workflows/test.yml`) must pass.
- All review threads must be resolved before the merge. An approving
  review is not required.
- Merge commits, squash merges and rebase merges are all allowed.
- The branch is deleted after the merge. A pull request stacked on that
  branch then targets `main` by itself.

Secret scanning with push protection is on, and so are Dependabot
security updates. Do not commit tokens, keys or passwords. CodeQL code
scanning runs on every pull request and on `main`, but it is not a
required check. Dependabot also opens a pull request each week to update
the GitHub Actions in `.github/workflows/`.

Every workflow must declare its own `permissions:`, as `test.yml` does.
Without that block a workflow gets the repository's default token, and
that token can write.

### One feature per pull request

Keep a pull request to one feature or one fix. Small pull requests are
easier to review and to test on a badge. If a feature needs a change
first, make that change its own pull request, and stack the feature on
it.

Write in the pull request:

- what it changes, and why;
- how you tested it (see below);
- what it costs in flash: the firmware's `FLASH` line, or the size of the
  changed apps;
- whether it changes the app API.

The pull request template asks for these. Refer to the issue it solves,
for example `(#52)` in the commit title, or `Fixes #52` in the
description.

### Commits

Look at `git log` for the style. A commit title is a short, plain
sentence that says what changes, for example "Factory test: only KEY
needs a hand, FACTORY_KEYS=0 for none". The body says what and why.

## Build

You need a RISC-V GCC: `riscv64-elf-gcc` (Arch, the default) or
`riscv64-unknown-elf-gcc` (Debian and Ubuntu, with `libnewlib-dev`). With
the Debian and Ubuntu toolchain, run `export PREFIX=riscv64-unknown-elf`
first, as CI does. The tests and `tools/deploy_webui.sh` run `make`
themselves, so a `PREFIX=` on your own `make` command is not enough. The
rest is in `ch32fun/`.

```sh
make -C ch32fun/minichlink                         # once: the flash tool
make MINICHLINK=ch32fun/minichlink ledmatrix.bin   # the firmware
make -C apps                                       # every app, into apps/build/
```

For the tests you also need `cc` (GCC or Clang, with AddressSanitizer),
Python 3 and Node.js. For `tools/badge.py` you need pyusb
(`pip install pyusb`).

## Flash budget

The firmware has 30 KB of flash. The item store and the persistent record
have the rest, so the firmware cannot grow past 30 KB: the link fails.
In November 2026 the firmware is about 27.5 KB, so there is little room.

Check the `FLASH` line that the build prints:

```
           FLASH:       27560 B        30 KB     89.71%
```

Put the old and the new number in the pull request. If a feature does
not have to be in the firmware, make it an app: an app takes space only
on the badges that store it. Shared code that many apps need belongs in
the firmware, once, through the app API.

## Tests

Every change comes with tests: a new test, or a change to an existing
one. CI runs all of them on each pull request, and you can run them
yourself:

```sh
tools/run_tests.sh             # every test, at the same time: about 90 s
tools/run_tests.sh four ir     # only the tests whose names start so
tools/run_tests.sh -j 1        # one test at a time: about 7 minutes
sh tools/four_test/run.sh      # one test, without the runner
```

`tools/run_tests.sh` builds the emulator and the apps first, then runs
the tests below, 2 per CPU at a time (`-j N` changes that). Most tests
wait for the emulator in real time, so they use little CPU. It prints
the log of each test, then a table of the results and the seconds each
took, and exits with an error if a test failed. A test that runs longer
than 5 minutes is stopped and fails (`HH27_TEST_TIMEOUT=600` changes
that). Ctrl-C stops every test and shows the end of the log of each
test that was still running. The runner needs bash 5 (on macOS:
`brew install bash`). Without GNU `timeout` the tests run with no time
limit.

A new test is a `tools/<name>/run.sh`; the runner finds it. It runs at
the same time as the others, so it must not write in the repository or
at a fixed path (use `mktemp -d`), must not use a fixed port, and must
not run `make clean`.

| Test | What it checks |
|---|---|
| `tools/app_test/run.sh` | the apps: game rules on the PC, no text drawn over in the same tick (every app alone for a while), then every app in the emulator with random buttons |
| `tools/codec_test/run.sh` | the web interface and the firmware decode animations the same way |
| `tools/brightness_test/run.sh` | the brightness: the start value, the USB command and `tools/badge.py brightness`, kept over a power cycle, the level on the grid (the menu, and B / D over what runs) |
| `tools/browse_test/run.sh` | a tap of C outside apps steps through the name tag, the animations and the texts, and skips the apps and any animation that cannot start (a store of apps only: the name tag stays); an app keeps C; long presses, C + D, and presses from the menu do not step |
| `tools/text_test/run.sh` | text items, from the web interface to the emulator |
| `tools/ir_test/run.sh` | the infrared codec, and raw frames from an app on the emulator |
| `tools/four_test/run.sh` | 4 in a row: rules on the PC, and whole games on two emulators |
| `tools/factory_test/run.sh` | the factory test on the emulator |
| `tools/bringup_test/run.sh` | `tools/badge.py bringup` lights the LED of each designator it prints, on the emulator |
| `tools/usb_test/run.sh` | the USB descriptors (every length, for Windows), and the C + D boot ROM chord on the emulator |
| `tools/board_test/run.sh` | `tools/extract_board_data.py` makes the same `webui/board_data.js` from the KiCad board (SKIP without the board) |
| `tools/charge_test/run.sh` | the charger's debounce on the PC; the charging screen and apps' `charging()` on the emulator, with USB power in and out, charging and no battery |
| `tools/emu/rv32_test/run.sh` | the emulator's RISC-V interpreter |
| `tools/isp_test/isp_test.js` | the web interface's firmware update, offline |

### The emulator

The emulator (`tools/emu`) builds the firmware's own C files for the PC
and runs the apps in a RISC-V interpreter. Use it to test firmware
behaviour without a badge:

```sh
make -C tools/emu
python3 tools/emu/emu.py --web
```

For automatic tests, `tools/emu/headless.py` drives the emulator without
a window: USB, buttons, infrared, USB power and charging, a fresh start,
and the text on the grid. The tests in
`tools/text_test`, `tools/ir_test` and `tools/four_test` show how to use
it.

### On a badge

The emulator does not replace the hardware. When a change touches the
hardware (the LEDs, USB, infrared, the buttons, standby, power, the
flash, timing), try it on a badge, and say in the pull request that you
did and what you saw. If you have no badge, say so, and ask someone with
a badge to test it.

For a newly assembled board, follow the checklist in `docs/BRINGUP.md`
first.

## The app API

`apps/hhapp.h` is the interface between the firmware and the apps, and
`docs/APP_API.md` explains it. Apps that are already on badges must keep
working with newer firmware, so:

- Add new members only at the end of `hh_api_t`. Do not reorder, remove
  or change existing members.
- Increase `HH_API_VERSION` in `apps/hhapp.h`, and add the new version to
  its comment, to the version table in `docs/APP_API.md`, and to the list
  under "Limits" in `apps/README.md`.
- Document every new member in `docs/APP_API.md`.
- Rebuild the App library with `make -C apps webui`, and commit
  `webui/apps/`. The web interface serves those files, so they must
  match the source.

Also run `make -C apps webui` and commit `webui/apps/` after any change
to an app.

## Layout tables

`layout.c` and `webui/layout_data.js` are generated from
`webui/board_data.js`. Do not edit them by hand. After a change to
the board data or to the generator, run:

```sh
python3 tools/gen_layout.py
```

and commit the result. CI checks that the committed tables match what
the generator makes.

For new hardware: set `HW_REV` in `tools/extract_board_data.py` to the
new commit of badgeteam/hackerhotel2027-hardware (full SHA), then run

```sh
python3 tools/extract_board_data.py
python3 tools/gen_layout.py
```

and commit `webui/board_data.js` and the tables together. This changes
the firmware tables. `tools/board_test/run.sh` (CI fetches the board at
`HW_REV`) checks that `webui/board_data.js` is what the script makes.

## No compatibility shims before the code freeze

The code freeze is in late January 2027, before the event on 19-21
February 2027. Until then you may change the app API, the USB protocol
and the storage formats when that makes the badge better. Do not add
compatibility code for older formats or older firmware. Bump the version
and update the web interface, the tools and the apps in the same pull
request.

## Flash a badge

Over USB, through the CH643's boot ROM. Do the steps in this order:

1. Start the flash, so that it waits for the boot ROM:

   ```sh
   until lsusb -d 1a86:55e0 >/dev/null; do sleep 0.1; done
   ch32fun/minichlink/minichlink -w ledmatrix.bin flash -b
   ```

2. On the badge, open the menu (hold **A**) and choose **UPDATE**, or
   hold **C** + **D** for 3 s.

The boot ROM waits about 5 s for a host. The web interface's
**Firmware** panel can also flash the badge (`docs/FIRMWARE_UPDATE.md`).
Flashing keeps the stored items, the name and the settings. On Linux you
need a udev rule: see `README.md`, "Flash over USB".

## Talk to a badge

`tools/badge.py` uses the same USB protocol as the web interface. It
lists, adds, runs and deletes items, sets the name, and more. The
comment at the top of the script lists the commands, and
`tools/badge.py --help` prints the same (with pyusb installed). Only
one program can have the badge open: click **Disconnect** in the web
interface first.

## Deploy the web interface

The web interface has two homes:

- **<https://badge.team/hackerhotel2027-tool/>**: the public site.
- **<https://scene.rs/star/>**: acceptance, for public testing before a
  change goes to the public site.

To publish it:

```sh
tools/deploy_webui.sh HOST:PATH         # dry run: shows what would change
tools/deploy_webui.sh HOST:PATH --go    # copy for real
```

The script deploys `webui/` as committed at `HEAD`, not your working
tree. It also builds the firmware of that commit for the **Firmware**
panel, so commit the firmware sources first. Deploy from `main`, after
the merge.

## License

By contributing, you agree that your contribution is licensed under the
MIT license in `LICENSE`. `ch32fun/` is a copy of
[ch32fun](https://github.com/cnlohr/ch32fun) and keeps its own MIT
license, in `ch32fun/LICENSE`.
