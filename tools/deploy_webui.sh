#!/bin/sh
# Publishes the web interface (webui/) to a web server over rsync/ssh.
#
#   tools/deploy_webui.sh HOST:PATH          dry run: show what would change
#   tools/deploy_webui.sh HOST:PATH --go     copy for real
#
# It deploys what is committed at HEAD (git archive), not the working tree,
# so the site matches a commit of this repository, and with it the
# firmware built from that commit. The page shows the commit in its footer
# (version.json) and warns when a badge's firmware is older than it expects.
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
