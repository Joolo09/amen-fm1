#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# AMEN's own screens (AMEN_SETUP=1: the 80s splash, the header's line, the LED gauges, the cut corners, the AMEN palette
# and font) through Felucca's renderer (fw/tests/ui_render.c): every screen in every palette. Felucca's suite runs
# with AMEN_SETUP=0 (tools/felucca_tests.sh), so this is where AMEN's look is checked. Fails on any layout lint finding
# (text off the screen, cut, overlapping, out of its cell), a GREY / MONO pixel off gray, or more 1 px alignment
# offsets than Felucca's own count (532, the same with Felucca's look). PNGs of the AMEN palette in build/amen-ui/.
# Run after tools/build_amen.sh (fw/build/gen).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/amen-ui
rm -rf "$out"; mkdir -p "$out/ppm" "$out/slot"
cd "$here/fw"
cc -O1 -w -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src -Ibuild/gen -Itests \
    "-DFELUCCA_VERSION=\"${1:-v0.3}\"" -o "$out/ui_render" tests/ui_render.c -lm
"$out/ui_render" "$out" "$out/slot" > "$out/run.log" 2>&1 || true
cat "$out/run.log"
grep -q " 0 lint findings" "$out/run.log" || { echo "AMEN UI: layout lint findings (build/amen-ui/report.txt):"; grep "^LINT\|^FIND" "$out/report.txt" | grep -v self-test | head -40; exit 1; }
grep -q " 0 GREY / MONO pixels off" "$out/run.log" || { echo "AMEN UI: GREY / MONO pixels off gray"; exit 1; }
grep -q "splash shows the version" "$out/run.log" || { echo "AMEN UI: the splash's lines"; grep "splash" "$out/report.txt"; exit 1; }
off=$(sed -n 's/.* \([0-9]*\) off by 1 px or more.*/\1/p' "$out/run.log")
[ "${off:-999}" -le 532 ] || { echo "AMEN UI: $off alignment offsets (Felucca's look: 532)"; exit 1; }
if python3 -c "import PIL" 2>/dev/null; then python3 tests/ui_render.py "$out" "$out/slot" > /dev/null; fi
echo "AMEN UI: every screen and palette clean (0 lint findings, GREY / MONO gray, $off alignment offsets as Felucca's)"
