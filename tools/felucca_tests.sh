#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Felucca's host test suite (fw/tests/run_tests.sh) on the AMEN sources and on Felucca 1.9.0 as released, and the
# failures that only AMEN has. The suite expects Felucca's power-on setup (its tracks, 120 BPM), so AMEN runs it with
# AMEN_SETUP=0, given by a wrapper of cc (the suite's sanitizer builds take only the first word of CC); what is left is
# the engines' swap (PHASE -> JUNGLE, TRIO -> REESE). The whole suite runs on AMEN (fold155_test, PHASE's, does not
# build here: a failure, not the end of the suite). The failures AMEN is expected to have, each with its reason:
# tools/felucca_tests.expected. Full logs in build/tests/. Run after tools/build_amen.sh (it makes fw/build/gen); needs
# JIELI_TOOLCHAIN and AC79_SDK as that does.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/tests
mkdir -p "$out"
# the failures in a log: lines with FAIL / error: / Error / failed, regress' GOLDEN CHANGED / GONE, CRASH and OVER
# BUDGET lines, and a test killed by a signal or stopped by a sanitizer (sh prints only "Segmentation fault",
# "Aborted", ..: the line is named after the test, its "== " line). Normalised: no time stamp, gcc's quotes in any
# locale, addresses 0xN, hashes H, numbers N
fails() {
    awk '/^== / { t = substr($0, 4) }
         /Segmentation fault|Aborted|Bus error|Illegal instruction|Floating point exception|ERROR: [A-Za-z]*Sanitizer|WARNING: ThreadSanitizer/ {
             print "CRASHED: " t ": " $0; next }
         { print }' "$1" |
        grep -E "FAIL|error:|Error|\bfailed\b|GOLDEN (CHANGED|GONE)|regress: CRASH|OVER BUDGET|^CRASHED: " | grep -vE "[[:space:]]ok$" |
        sed -E "s/^[0-9T:.Z-]+ //; s/‘|’/'/g; s/0x[0-9a-fA-F]+/0xN/g; s/\b[0-9a-f]{16}\b/H/g; s/[0-9]+(\.[0-9]+)?(e[-+]?[0-9]+)?/N/g" | sort -u
}

printf '#!/bin/sh\nexec cc -DAMEN_SETUP=0 "$@"\n' > "$out/amen-cc"
chmod +x "$out/amen-cc"
(cd "$here/fw" && CC="$out/amen-cc" sh tests/run_tests.sh > "$out/amen.log" 2>&1) && echo "AMEN: the suite passed" || echo "AMEN: the suite reported failures"

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
comm -23 "$out/amen.fails" "$out/felucca.fails" > "$out/amen-only.fails"
echo "== failures only on the AMEN sources: $(wc -l < "$out/amen-only.fails")"
sed 's/^/   /' "$out/amen-only.fails" | head -80
# expected (tools/felucca_tests.expected: lines as fails() prints them; # starts a comment, the reason)
sed '/^#/d; /^$/d' "$here/tools/felucca_tests.expected" | sort -u > "$out/expected.fails"
comm -23 "$out/amen-only.fails" "$out/expected.fails" > "$out/unexpected.fails"
if [ -s "$out/unexpected.fails" ]; then
    echo "== UNEXPECTED failures on the AMEN sources:"
    sed 's/^/   /' "$out/unexpected.fails"
    exit 1
fi
echo "== no unexpected failures"
