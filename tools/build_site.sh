#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Assembles the installer site (GitHub Pages) in build/site:
#   site/                 the page, its script, Felucca's WebMIDI updater (site/vendor/)
#   firmware/             AMEN OS (build/os: tools/build_os.sh first) and amen.json, the licences of what the package
#                         holds (LICENSE: GPL-3.0; felucca-bsp's MPL-2.0; the JieLi SDK files' Apache-2.0) and the source
#                         of this commit (amen-source.tar.gz)
#   demo.mp3
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/site
[ -f "$here/build/os/amen-os.json" ] || { echo "build_site: no build/os/amen-os.json (run tools/build_os.sh)" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out/firmware"
cp -R "$here/site/." "$out/"
cp "$here/docs/demo.mp3" "$out/demo.mp3"
cp "$here"/build/os/*.fwsc "$out/firmware/"
cp "$here/build/os/amen-os.json" "$out/firmware/amen.json"
mkdir -p "$out/firmware/LICENSES"
cp "$here/LICENSE" "$out/firmware/LICENSE"
cp "$here/fw/firmware/bsp/LICENSE" "$out/firmware/LICENSES/MPL-2.0-felucca-bsp.txt"
cp "$here/fw/firmware/bsp/NOTICE" "$out/firmware/LICENSES/NOTICE-felucca-bsp.txt"
cp "$here/fw/LICENSES/Apache-2.0.txt" "$out/firmware/LICENSES/Apache-2.0-JieLi-SDK.txt"
git -C "$here" archive --format=tar.gz --prefix=amen-fm1/ -o "$out/firmware/amen-source.tar.gz" HEAD
touch "$out/.nojekyll"
echo "$out"
