#!/bin/sh
# The factory test (factory.c) on the emulator
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/tools/emu"
python3 "$here/emu_factory_test.py"
