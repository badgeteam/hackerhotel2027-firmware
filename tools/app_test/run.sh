#!/bin/sh
# The apps: their game logic on the PC with a stand-in for the firmware's
# API (mock_hh.h), then every app in the emulator, with random buttons.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for t in simon whack; do
    cc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -I"$root" -I"$root/apps" \
        -o "$out/$t" "$here/${t}_test.c" "$root/layout.c"
    "$out/$t"
done
make -s -C "$root/apps" clean all >/dev/null  # only the apps of this checkout
make -s -C "$root/tools/emu"
python3 "$here/emu_smoke.py" "$root/tools/emu/build/hh27-emu" "$root"/apps/build/*.hhapp
