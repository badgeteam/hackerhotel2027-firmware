#!/bin/sh
# Text item tests: the web interface's side (text_item_test.js), then the
# same payloads stored and played on the emulator (emu_text_test.py).
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
node "$here/text_item_test.js" "$out"
make -s -C "$root/tools/emu"
python3 "$here/emu_text_test.py" "$root/tools/emu/build/hh27-emu" "$out"
