#!/bin/sh
# The name tag's reaction to banana shots (nametag.c) on the emulator
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/apps" >/dev/null  # the banana and fire apps, for shots during an app
make -s -C "$root/tools/emu"
python3 "$here/emu_nametag_test.py"
