#!/bin/sh
# 4 in a row (apps/four): its rules on the PC, then whole games on two
# emulators linked by infrared.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -I"$root/apps" -o "$out/four_test" "$here/four_host_test.c"
"$out/four_test"
make -s -C "$root/apps" build/four.hhapp >/dev/null
make -s -C "$root/tools/emu"
python3 "$here/emu_four_test.py" "$root/apps/build/four.hhapp"
