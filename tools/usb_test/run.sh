#!/bin/sh
# USB: the descriptors on the host (usb_descriptors.h: every length, the BOS
# descriptor, the Microsoft OS 2.0 set, the WebUSB URL, the serial number),
# then the C + D boot ROM chord on the emulator
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$root" -o "$out/usb_descriptor_test" \
    "$here/usb_descriptor_test.c"
"$out/usb_descriptor_test"
make -s -C "$root/apps" APPS_OUT="$out" "$out/factory.hhapp" >/dev/null
make -s -C "$root/tools/emu"
python3 "$here/emu_boot_chord_test.py" "$out/factory.hhapp"
