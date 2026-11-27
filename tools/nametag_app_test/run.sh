#!/bin/sh
# The NAMETAG app (apps/nametag) on emulated badges linked by infrared:
# badge friends, the panel, a power cycle, a plain firmware badge, the
# banana, the style kept, a friend under the charging screen. Its logic on
# the PC is tools/app_test/nametag_test.c; its charging screen is tested in
# tools/charge_test.
set -e
here=$(dirname "$0")
root="$here/../.."
make -s -C "$root/apps" build/nametag.hhapp >/dev/null
make -s -C "$root/tools/emu"
python3 "$here/emu_nametag_app_test.py" "$root/apps/build/nametag.hhapp"
