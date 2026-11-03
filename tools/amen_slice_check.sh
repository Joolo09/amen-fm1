#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# AMEN: the sample engines' reads under the sanitizers (fw/AMEN.md, "Sample data reads").
#  1. fw/tests/amen_slice_bounds_test.c with ASan + UBSan (all of it but shift: Felucca's DSP shifts negative values,
#     its own drumfuzz run leaves shift out too): SLICE plays a user slot whose data end where the image ends, every
#     slice; voices given bad positions stop, reading nothing outside their zones.
#  2. Felucca's SLICE and SAMPLE tests (user slots: a loop, PIANO HD) and the UI renderer (the SLICES page on a user
#     slot: the report that started this) under UBSan with its bounds checks: no runtime error.
# Run after tools/build_amen.sh (fw/build/gen). Writes build/amen-slice/.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/amen-slice
rm -rf "$out"; mkdir -p "$out/ui/ppm" "$out/ui_slot" "$out/slice_demo" "$out/sample_demo"
cd "$here/fw"
INC="-Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src -Ibuild/gen"
UB="cc -O1 -g -fsanitize=undefined -fno-sanitize=shift -fno-omit-frame-pointer -w $INC"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=all -fno-omit-frame-pointer -w $INC \
    -o "$out/amen_slice_bounds_test" tests/amen_slice_bounds_test.c -lm
"$out/amen_slice_bounds_test"

python3 tests/slice_loop.py "$out/loop" > /dev/null
python3 tools/fm1_sample_upload.py build LOOP "$out/loop" "$out/loop.wav:60" > /dev/null
$UB -o "$out/slice_test" tests/slice_test.c -lm
"$out/slice_test" "$out/loop" "$out/slice_demo" > "$out/slice.log" 2>&1 || { tail -20 "$out/slice.log"; exit 1; }
if grep -q '^    {"PIANO", 0, 5},' build/gen/felucca_samples.h; then
    python3 tools/gen_samples.py --user-slot PIANO "PIANO HD" "$out/PIANO_HD" > /dev/null
    $UB -o "$out/sample_test" tests/sample_test.c -lm
    "$out/sample_test" "$out/PIANO_HD" "$out/sample_demo" > "$out/sample.log" 2>&1 || { tail -20 "$out/sample.log"; exit 1; }
else
    echo "AMEN samples: no CC0 PIANO in this build, sample_test not run" > "$out/sample.log"
fi
$UB -Itests "-DFELUCCA_VERSION=\"${1:-v0.2}\"" -o "$out/ui_render" tests/ui_render.c -lm
"$out/ui_render" "$out/ui" "$out/ui_slot" > "$out/ui_render.log" 2>&1 || true   # (its exit: amen_ui_check.sh)

if grep -h "runtime error" "$out/slice.log" "$out/sample.log" "$out/ui_render.log"; then
    echo "AMEN samples: UBSan reports above (build/amen-slice/*.log)"
    exit 1
fi
echo "AMEN samples: SLICE inside its zones under ASan / UBSan; slice_test, sample_test, ui_render: no UBSan report"
