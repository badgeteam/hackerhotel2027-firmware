#!/bin/sh
# Infrared: the codec (ir_codec.c) on the PC, packets and NEC frames through a
# model of the receiver, with jitter, noise and bit errors; then raw frames
# from a test app (raw_app.c), built as an app, on the emulator. The name swap
# is the NAMETAG app's: tools/nametag_app_test.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -I"$root" -o "$out/ir_test" "$here/ir_test.c" "$root/ir_codec.c"
"$out/ir_test"
make -s -C "$root/tools/emu"
# Raw frames (ir_send_raw), flags as apps/Makefile
"${PREFIX:-riscv64-elf}-gcc" -march=rv32imac -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -msmall-data-limit=0 \
    -Wall -Wextra -I"$root/apps" -T "$root/apps/app.ld" -Wl,--gc-sections -Wl,--no-relax -Wl,--no-warn-rwx-segments \
    -nostartfiles -o "$out/raw_app.elf" "$here/raw_app.c" "$root/apps/sdk.c"
"${PREFIX:-riscv64-elf}-objcopy" -O binary -j .image "$out/raw_app.elf" "$out/raw_app.hhapp"
python3 "$here/emu_raw_test.py" "$out/raw_app.hhapp"
