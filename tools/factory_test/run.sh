#!/bin/sh
# The factory test (apps/factory) on the emulator, with the factory image
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
make -s -C "$root/tools/emu"
# its own copy of the app: another test may clean apps/build meanwhile
make -s -C "$root/apps" APPS_OUT="$out" "$out/factory.hhapp" >/dev/null
HH_FACTORY_APP="$out/factory.hhapp" python3 "$here/emu_factory_test.py"
