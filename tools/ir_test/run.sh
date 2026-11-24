#!/bin/sh
# Infrared: the codec (ir_codec.c) on the PC, packets and NEC frames through a
# model of the receiver, with jitter, noise and bit errors; then the name swap.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -I"$root" -o "$out/ir_test" "$here/ir_test.c" "$root/ir_codec.c"
"$out/ir_test"
# The name swap on two emulators linked by infrared
make -s -C "$root/tools/emu"
python3 "$here/emu_greet_test.py"
