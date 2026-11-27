#!/bin/sh
# Browsing with C outside apps (ledmatrix.c) on the emulator
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/apps" >/dev/null  # the fire and nametag apps, which C skips
make -s -C "$root/tools/emu"
python3 "$here/emu_browse_test.py"
