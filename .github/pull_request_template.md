## What and why

<!-- What does this change, and why? One feature or fix per pull request.
Refer to the issue, for example "Fixes #52". -->

## How it was tested

- [ ] The tests pass: `tools/*/run.sh`, `tools/emu/rv32_test/run.sh`, `node tools/isp_test/isp_test.js`
- [ ] New or changed tests cover this change
- [ ] Tried in the emulator (`python3 tools/emu/emu.py --web`)
- [ ] Tried on a badge (needed when it touches the hardware)

<!-- What did you try, and what did you see? If it touches the hardware and
you have no badge, say so. -->

## Size

<!-- The firmware's FLASH line from the build, before and after (the budget
is 30 KB), and the size of each app that changed. Write "none" if
nothing that runs on the badge changed. -->

- Firmware FLASH: before ... B, after ... B
- Apps:

## App API

- [ ] No change to the app API

Or, if it changes:

- [ ] New members only at the end of `hh_api_t` in `apps/hhapp.h`
- [ ] `HH_API_VERSION` increased, and the version listed in `apps/hhapp.h`, `docs/APP_API.md` and `apps/README.md`
- [ ] New members documented in `docs/APP_API.md`
- [ ] `make -C apps webui` run, and `webui/apps/` committed
