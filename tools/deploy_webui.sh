#!/bin/sh
# Publishes the web interface (webui/) to a web server over rsync/ssh.
#
#   tools/deploy_webui.sh HOST:PATH          dry run: show what would change
#   tools/deploy_webui.sh HOST:PATH --go     copy for real
#
# It deploys what is committed at HEAD (git archive), not the working tree,
# so the site matches a commit of this repository. It also builds the
# firmware of that commit (the firmware sources must be committed) into
# firmware/, which the page's Firmware panel offers to flash. The page shows
# the commit in its footer (version.json) and compares it with a badge's.
#
# The server must serve it over HTTPS (WebUSB needs a secure context). A
# .htaccess is included for Apache: the page's video import plays the
# chosen file through a blob: URL, which a strict Content-Security-Policy
# must allow (media-src blob:).
set -eu

target=${1:?usage: tools/deploy_webui.sh HOST:PATH [--go]}
cd "$(dirname "$0")/.."

if [ -n "$(git status --porcelain -- webui)" ]; then
	echo "webui/ has uncommitted changes; they are not deployed (HEAD is)." >&2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive HEAD webui | tar -x -C "$tmp"
commit=$(git rev-parse --short HEAD)
date=$(git log -1 --format=%cs HEAD)
printf '{"commit": "%s", "date": "%s"}\n' "$commit" "$date" > "$tmp/webui/version.json"
# The firmware the page's Firmware panel flashes: built from this commit
if [ -n "$(git status --porcelain -- '*.c' '*.h' Makefile apps/hhapp.h tools/saverestore.S)" ]; then
	echo "The firmware sources have uncommitted changes: commit them first, so the" >&2
	echo "site's firmware is the one of $commit." >&2
	exit 1
fi
make -B -s MINICHLINK=ch32fun/minichlink ledmatrix.bin >/dev/null
mkdir -p "$tmp/webui/firmware"
cp ledmatrix.bin "$tmp/webui/firmware/ledmatrix.bin"
fw=$(grep -ao 'HH27-STARFISH-FW S[0-9]* [^[:cntrl:]]*' ledmatrix.bin | cut -d' ' -f3)
printf '{"commit": "%s", "date": "%s", "bytes": %s}\n' "$fw" "$date" "$(stat -c %s ledmatrix.bin)" > "$tmp/webui/firmware/firmware.json"

cat > "$tmp/webui/.htaccess" <<'HTACCESS'
# The badge web interface (tools/deploy_webui.sh in hackerhotel2027-firmware).
# Scripts and styles are files here; the video import needs blob: media.
Header always set Content-Security-Policy "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'"
HTACCESS

if [ "${2:-}" = "--go" ]; then
	DRY=
else
	DRY=--dry-run
	echo "Dry run. Add --go to copy." >&2
fi
# --delete only inside the target directory: it then holds exactly this commit's webui/
# shellcheck disable=SC2086
rsync -rltv --checksum --delete --chmod=D775,F664 $DRY "$tmp/webui/" "$target/"
echo "web interface $commit ($date)" >&2
