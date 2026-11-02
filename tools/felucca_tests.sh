#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Felucca's host test suite (fw/tests/run_tests.sh) on the AMEN sources and on Felucca 1.9.0 as released, and the
# failures that only AMEN has. The suite expects Felucca's power-on setup (its tracks, 120 BPM), so AMEN runs it with
# AMEN_SETUP=0: what is left is the engines' swap (PHASE -> JUNGLE, TRIO -> REESE). Expected there: the tests of PHASE
# and TRIO themselves (fold155_test and the like) and the renders of engines 2 and 6. Full logs in build/tests/. Run after tools/build_amen.sh (it makes fw/build/gen);
# needs JIELI_TOOLCHAIN and AC79_SDK as that does.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/tests
mkdir -p "$out"
fails() { grep -E "FAIL|error:|Error|\bfailed\b" "$1" | grep -vE "[[:space:]]ok$" | sed -E 's/^[0-9T:.Z-]+ //; s/[0-9]+(\.[0-9]+)?(e[-+]?[0-9]+)?/N/g' | sort -u; }

(cd "$here/fw" && CC="cc -DAMEN_SETUP=0" sh tests/run_tests.sh > "$out/amen.log" 2>&1) && echo "AMEN: the suite passed" || echo "AMEN: the suite reported failures"

ref=$out/felucca-1.9.0
if [ ! -d "$ref" ]; then
    curl -fsSL --retry 3 https://github.com/hugelton/Felucca/releases/download/v1.9.0/felucca-1.9.0-source.tar.gz | tar -xz -C "$out"
fi
(cd "$ref" && python3 tools/build.py --release 1.9.0 > "$out/felucca-build.log" 2>&1 && sh tests/run_tests.sh > "$out/felucca.log" 2>&1) \
    && echo "Felucca 1.9.0: the suite passed" || echo "Felucca 1.9.0: the suite reported failures"

fails "$out/amen.log" > "$out/amen.fails"
fails "$out/felucca.log" > "$out/felucca.fails"
echo "== failures in Felucca 1.9.0 itself: $(wc -l < "$out/felucca.fails")"
sed 's/^/   /' "$out/felucca.fails" | head -40
echo "== failures only on the AMEN sources: $(comm -23 "$out/amen.fails" "$out/felucca.fails" | wc -l)"
comm -23 "$out/amen.fails" "$out/felucca.fails" | sed 's/^/   /' | head -80
