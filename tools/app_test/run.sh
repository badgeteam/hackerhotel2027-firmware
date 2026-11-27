#!/bin/sh
# The apps: their game logic on the PC with a stand-in for the firmware's
# API (mock_hh.h), then every app in the emulator, with random buttons.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# A text pixel that the app draws over in the same tick fails (mock_hh.h);
# -g lets the report name the line that drew over it
export MOCK_OVERDRAW=1
for t in util simon whack breakout flappy pong startris chase maze hilo snake eye banana nametag bottle four tvbon; do
    cc -std=gnu11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I"$root" -I"$root/apps" \
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
# Every app alone for a while: its demo, then random buttons (overdraw_run.c)
for d in "$root"/apps/*/; do
    a=$(basename "$d")
    [ -f "$d$a.c" ] || continue
    more=$(ls "$d"*.c | grep -v "/$a\.c\$" || true)  # an app of more files (nametag: styles.c)
    cc -std=gnu11 -Wall -Wextra -O1 -g -I"$root" -I"$root/apps" -DAPP_FILE="\"$d$a.c\"" -DMOCK_APP="\"$a\"" \
        -o "$out/od_$a" "$here/overdraw_run.c" $more "$root/layout.c" -lm
    "$out/od_$a" 2
done
# No make clean: other tests use apps/build while this one runs
# (tools/run_tests.sh runs them at once)
make -s -C "$root/apps" >/dev/null
make -s -C "$root/tools/emu"
# Only the apps of this checkout (the dirs with a .c file, as apps/Makefile):
# apps/build can still hold apps of another branch
hhapps=""
for d in "$root"/apps/*/; do
    ls "$d"*.c >/dev/null 2>&1 && hhapps="$hhapps $root/apps/build/$(basename "$d").hhapp"
done
python3 "$here/emu_smoke.py" "$root/tools/emu/build/hh27-emu" $hhapps
python3 "$here/recommended_test.py"
