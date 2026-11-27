#!/bin/sh
# tools/badge.py bringup (the LED walk) on the emulator
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/tools/emu"
python3 "$here/emu_bringup_test.py"
