#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Assembles the installer site (GitHub Pages) in build/site:
#   site/                 the page, its script, Felucca's WebMIDI updater (site/vendor/)
#   firmware/             the AMEN firmware (build/amen: tools/build_amen.sh first), amen.json, the licences of the
#                         package (fw/LICENSE, LICENSING.md, LICENSES/) and the source of this commit (amen-source.tar.gz)
#   demo.mp3
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/site
[ -f "$here/build/amen/amen.json" ] || { echo "build_site: no build/amen/amen.json (run tools/build_amen.sh)" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out/firmware"
cp -R "$here/site/." "$out/"
cp "$here/docs/demo.mp3" "$out/demo.mp3"
cp "$here"/build/amen/*.fwsc "$here/build/amen/amen.json" "$out/firmware/"
cp "$here/fw/LICENSE" "$here/fw/LICENSING.md" "$out/firmware/"
cp -R "$here/fw/LICENSES" "$out/firmware/"
git -C "$here" archive --format=tar.gz --prefix=amen-fm1/ -o "$out/firmware/amen-source.tar.gz" HEAD
touch "$out/.nojekyll"
echo "$out"
