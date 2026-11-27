#!/bin/sh
# The charging screen (charge.c) and the charger's debounce (power.c): the
# debounce on the PC, then the screen and apps' charging() on the emulator,
# with its USB power and charger (headless.py: usb_power(), charging(),
# no_battery())
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -DHH_EMU -I"$root/tools/emu/stub" -I"$root" \
    -o "$out/power_test" "$here/power_test.c" "$root/power.c"
"$out/power_test"
# charging_app.c: an app that prints what charging() returns
"${PREFIX:-riscv64-elf}-gcc" -march=rv32imac -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -msmall-data-limit=0 \
    -Wall -Wextra -I"$root/apps" -T "$root/apps/app.ld" -Wl,--no-relax -Wl,--no-warn-rwx-segments -nostartfiles \
    -o "$out/charging.elf" "$here/charging_app.c" "$root/apps/sdk.c"
"${PREFIX:-riscv64-elf}-objcopy" -O binary -j .image "$out/charging.elf" "$out/charging.hhapp"
make -s -C "$root/apps" APPS_OUT="$out" "$out/factory.hhapp" >/dev/null
make -s -C "$root/tools/emu"
python3 "$here/emu_charge_test.py" "$out/charging.hhapp" "$out/factory.hhapp"
