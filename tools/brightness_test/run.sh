#!/bin/sh
# The brightness (ledmatrix.c, menu.c, webusb_protocol.c) on the emulator:
# the start brightness, the USB command, and the level on the grid
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/tools/emu"
python3 "$here/emu_brightness_test.py"
