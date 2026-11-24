#!/bin/sh
# The apps: their game logic on the PC with a stand-in for the firmware's
# API (mock_hh.h), then every app in the emulator, with random buttons.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for t in util simon whack breakout flappy pong startris chase maze hilo snake eye; do
    cc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -I"$root" -I"$root/apps" \
        -o "$out/$t" "$here/${t}_test.c" "$root/layout.c" -lm
    "$out/$t"
done
# The animations: each app built under a prefix, into one test
objs=""
for a in ripple galaxy heart twinkle life fire hotel; do
    extra=""
    [ "$a" = life ] && extra="-DLIFE"
    cc -std=gnu11 -O1 -fsanitize=address,undefined -I"$root" -I"$root/apps" -DAPP_PREFIX=$a $extra \
        -DAPP_FILE="\"$root/apps/$a/$a.c\"" -c "$here/anim_wrap.c" -o "$out/$a.o"
    objs="$objs $out/$a.o"
done
cc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -I"$root" -I"$root/apps" \
    -o "$out/anim" "$here/anim_test.c" $objs "$root/layout.c" -lm
"$out/anim"
make -s -C "$root/apps" clean all >/dev/null  # only the apps of this checkout
make -s -C "$root/tools/emu"
python3 "$here/emu_smoke.py" "$root/tools/emu/build/hh27-emu" "$root"/apps/build/*.hhapp
