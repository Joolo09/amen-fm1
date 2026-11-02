#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Assembles the installer site (GitHub Pages) in build/site:
#   site/ (the page, its scripts, Felucca's WebMIDI updater in site/vendor/)
#   fep/        the AMEN plugins (build/fm1/*.fep: make fm1 first)
#   firmware/   Felucca's release package, its source and licences (downloaded, the package's digest checked)
#   demo.mp3
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/site
FEL_VER=1.9.0
FEL_SHA=08c1408019d01a3edc6e2f37bb61a24d3028dcfa5ed9570d6228a1eb6e75e0a4
REL=https://github.com/hugelton/Felucca/releases/download/v$FEL_VER
rm -rf "$out"
mkdir -p "$out/fep" "$out/firmware"
cp -R "$here/site/." "$out/"
cp "$here/docs/demo.mp3" "$out/demo.mp3"
for f in jungle.s0.fep reese.s1.fep; do
    [ -f "$here/build/fm1/$f" ] || { echo "build_site: no build/fm1/$f (run make fm1)" >&2; exit 1; }
    cp "$here/build/fm1/$f" "$out/fep/"
done
fw=$out/firmware
curl -fsSL --retry 3 -o "$fw/felucca-$FEL_VER.fwsc" "$REL/felucca-$FEL_VER.fwsc"
echo "$FEL_SHA  $fw/felucca-$FEL_VER.fwsc" | sha256sum -c -
curl -fsSL --retry 3 -o "$fw/felucca-$FEL_VER-source.tar.gz" "$REL/felucca-$FEL_VER-source.tar.gz"
# the licences of the package: Felucca's LICENSE, LICENSING.md and LICENSES/ from its source
tmp=$(mktemp -d)
tar -xzf "$fw/felucca-$FEL_VER-source.tar.gz" -C "$tmp"
src=$(dirname "$(find "$tmp" -maxdepth 3 -name LICENSING.md | head -1)")
[ -f "$src/LICENSE" ] || { echo "build_site: no LICENSE in the Felucca source" >&2; exit 1; }
cp "$src/LICENSE" "$src/LICENSING.md" "$fw/"
[ -d "$src/LICENSES" ] && cp -R "$src/LICENSES" "$fw/"
rm -rf "$tmp"
touch "$out/.nojekyll"
echo "$out"
