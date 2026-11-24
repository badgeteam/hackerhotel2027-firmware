#!/bin/sh
# LA04 tests: the web interface's codec round trip (codec_test.js), then
# the same patterns through the firmware's pattern_player.c on the host.
set -e
here=$(dirname "$0")
root="$here/../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
node "$here/codec_test.js" "$out"
cc -std=c11 -Wall -Wextra -O2 -fsanitize=address,undefined -DLA04_RAM_IN_BSS -I"$here/stub" -I"$root" -o "$out/player_test" \
    "$here/player_test.c" "$root/pattern_player.c" "$root/la04.c" "$root/layout.c"
"$out/player_test" "$out" $(cd "$out" && ls *.ledanim | sed 's/\.ledanim$//')
