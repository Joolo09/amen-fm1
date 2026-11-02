#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds AMEN OS for the FM-1 into build/os/:
#   amen-os.bin            the app (os/fm1/main.c on felucca-bsp), linked at 0x02000120 like any FM-1 app
#   amen-os-VERSION.fwsc   the installable package: the app, the update loader (felucca-bsp's, built here), three
#                          files of the JieLi AC79 SDK (Apache-2.0)
#   amen-os.json           version, identity, SHA-256 (the installer site)
# Needs JIELI_TOOLCHAIN (the JieLi Linux toolchain) and AC79_SDK (the JieLi AC79 SDK checkout).
#   sh tools/build_os.sh 0.3
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
ver=${1:-0.3}
tc=${JIELI_TOOLCHAIN:?JIELI_TOOLCHAIN must point at the JieLi Linux toolchain}
bsp=$here/fw/firmware/bsp
out=$here/build/os
product="FM-1_7$(echo "$ver" | tr -d '.' | cut -c1-2)"
rm -rf "$out"; mkdir -p "$out"
python3 "$here/tools/gen_tables.py" "$out/fe_tables.h"
CC="$tc/pi32v2/bin/clang -target pi32v2"
$CC -c "$bsp/chip/crt0.S" -o "$out/crt0.o"
$CC -c "$bsp/chip/fm1_vec.S" -o "$out/vec.o"
$CC -c "$bsp/chip/fm1_isr.S" -o "$out/isr.o"
$CC -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function "-DAMEN_ID=\"$product\"" "-DAMEN_VERSION=\"$ver\"" \
    -I"$bsp/chip" -I"$bsp/boards/fm1" -I"$out" -I"$here/sdk/src" -I"$here/engines" -I"$here/os" \
    -c "$here/os/fm1/main.c" -o "$out/main.o"
"$tc/pi32v2/bin/ld" -T "$here/os/fm1/app.ld" "$out/crt0.o" "$out/vec.o" "$out/isr.o" "$out/main.o" \
    -o "$out/amen-os.elf"
"$tc/common/bin/objcopy" -O binary -j .text "$out/amen-os.elf" "$out/text.bin"
"$tc/common/bin/objcopy" -O binary -j .data "$out/amen-os.elf" "$out/data.bin"
"$tc/common/bin/objcopy" -O binary -j .ram_text "$out/amen-os.elf" "$out/rt.bin"
"$tc/common/bin/objdump" -t "$out/amen-os.elf" > "$out/syms.txt"
"$tc/common/bin/objdump" -h "$out/amen-os.elf" > "$out/sections.txt"
python3 - "$out" <<'PY'
import re, sys
out = sys.argv[1]
syms = open(f"{out}/syms.txt").read()
def sym(n):
    return int(re.search(rf"^([0-9a-f]+) .*\s{n}$", syms, re.M).group(1), 16)
load, rt_load, start = sym("_data_load"), sym("_rt_load"), sym("_start")
assert start == 0x02000120, hex(start)
img = bytearray(open(f"{out}/text.bin", "rb").read())
for at, part in sorted([(rt_load, open(f"{out}/rt.bin", "rb").read()), (load, open(f"{out}/data.bin", "rb").read())]):
    if part:
        assert at - 0x02000120 >= len(img), "sections overlap"
        img += b"\xff" * (at - 0x02000120 - len(img)) + part
img += b"\xff" * (-len(img) % 4)
assert len(img) <= 0x8DFBC, f"app {len(img)} B over the app slot"
open(f"{out}/amen-os.bin", "wb").write(img)
# every allocated section where app.ld puts it: XIP code, RAMTEXT, RAM, NOINIT (an orphan elsewhere would fault)
regions = [(0x02000120, 0x02000120 + 0x8DFBC), (0x01C00000, 0x01C06000), (0x01C08000, 0x01C08000 + 96 * 1024),
           (0x01C7C000, 0x01C7C000 + 0x3D50)]
print(open(f"{out}/sections.txt").read())
for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+.*\n\s*(.*)$", open(f"{out}/sections.txt").read(), re.M):
    name, size, vma, flags = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
    if "ALLOC" in flags and size and not any(lo <= vma and vma + size <= hi for lo, hi in regions):
        raise SystemExit(f"section {name} at {vma:#x} (+{size}) outside the memory map")
for n in ("fm1_cstart", "fm1_alnk0_irq", "fm1_timer5_irq", "isr_alnk0", "isr_timer5", "_etext"):
    print(f"{n:16s} {sym(n):#010x}")
ram = sym("_bss_end") - sym("_data_start")
print(f"amen-os.bin: {len(img)} B; RAM .data+.bss {ram} B of {96 * 1024}")
assert ram <= 96 * 1024 - 2048, "RAM over budget"
PY
# the update loader (felucca-bsp's, built by Felucca's build.py as Felucca does)
(cd "$here/fw" && JIELI_TOOLCHAIN="$tc" python3 -c "import sys; sys.path.insert(0, 'tools'); sys.argv = ['build.py']; import build; build.build_loader()")
python3 "$here/fw/tools/fm1pkg_make.py" "$out/amen-os.bin" "$here/fw/build/loader/ota.bin" "$out/amen-os-$ver.fwsc" \
    --product "$product" --sdk "${AC79_SDK:?AC79_SDK must point at the JieLi AC79 SDK}"
python3 - "$out/amen-os-$ver.fwsc" "$ver" "$out/amen-os.json" <<'PY'
import hashlib, json, sys
raw = open(sys.argv[1], "rb").read()
product = "".join(chr((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF) for i in range(20) if raw[i * 0x30 + 0x2F] != 0x7D)
assert product.startswith("FM-1_7"), product
assert b"FELUCCA-LOADER-1" in raw, "no update loader in the package"
json.dump({"version": sys.argv[2], "product": product, "file": "firmware/amen-os-%s.fwsc" % sys.argv[2],
           "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}, open(sys.argv[3], "w"))
print(sys.argv[1], len(raw), "B, identity", product)
PY
