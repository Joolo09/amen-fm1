#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds the AMEN-FM1 plugins for the FM-1, as Felucca-Engines' tools/build_plugin_fm1.sh does (the same slot
# addresses, flags and packer; sdk/tools/ keeps the original):
#   JUNGLE -> build/fm1/jungle.s0.fep   (slot 0: 8 KiB of code and constants)
#   REESE  -> build/fm1/reese.s1.fep    (slot 1: 7.5 KiB)
# Needs the JieLi toolchain (clang for pi32v2): Felucca's tools/get_toolchain.sh installs it into ~/.jieli/toolchain
# (JIELI_TOOLCHAIN overrides). It runs on Linux x86-64; elsewhere in Docker (JIELI_DOCKER=1 forces it).
#   sh tools/build_fm1.sh                 both plugins in their own slots
#   sh tools/build_fm1.sh jungle 1        one plugin, built for the other slot
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
tc=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}
[ -x "$tc/pi32v2/bin/clang" ] || { echo "build_fm1: no JieLi toolchain in $tc (see docs/BUILD.md)" >&2; exit 1; }
out=$here/build/fm1
mkdir -p "$out"

native=0
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] && native=1
case "${JIELI_DOCKER:-}" in 0) native=1 ;; 1) native=0 ;; esac
run() {   # a toolchain binary ($1, relative to the toolchain); the repository is /amen inside Docker
    t=$1; shift
    if [ $native = 1 ]; then
        "$tc/$t" "$@"
    else
        docker run --rm --platform linux/amd64 -v "$here:/amen" -v "$tc:/opt/jieli:ro" -w /amen \
            debian:bookworm-slim "/opt/jieli/$t" "$@"
    fi
}
if [ $native = 1 ]; then R=$here; else R=/amen; fi

build() {   # name slot
    name=$1; slot=$2
    work=$out/$name.s$slot.work
    W=$R/build/fm1/$name.s$slot.work
    mkdir -p "$work/tables"
    python3 "$here/tools/gen_tables.py" --shared "$work/tables/fe_tables.h"
    if [ "$slot" = 0 ]; then flash=0x93000; size=0x2000; ram=0x01C72000; else flash=0x95000; size=0x1E00; ram=0x01C73000; fi
    xip=$(printf '0x%08X' $((0x02000000 + flash - 0x4000)))
    cat > "$work/slot.ld" <<LD
MEMORY
{
    XIP (rx) : ORIGIN = $xip + 96, LENGTH = $size - 96
    RAM (rw) : ORIGIN = $ram, LENGTH = 0x1000
}
SECTIONS
{
    .text : { KEEP(*(.text.felucca_plugin)) *(.text*) *(.rodata*) . = ALIGN(4); } > XIP
    .data : { . = ALIGN(4); _fe_data = .; *(.data*) *(.sdata*) . = ALIGN(4); _fe_edata = .; } > RAM AT > XIP
    _fe_data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _fe_bss = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _fe_ebss = .; } > RAM
    /DISCARD/ : { *(.comment) *(.note*) }
}
LD
    run pi32v2/bin/clang -target pi32v2 -Os -ffunction-sections -fdata-sections -fno-builtin -Wall \
        -Wno-unused-function -I"$W/tables" -I"$R/sdk/src" -I"$R/engines" -c "$R/plugins/$name.c" -o "$W/$name.o"
    run pi32v2/bin/ld -T "$W/slot.ld" --gc-sections -e felucca_plugin "$W/$name.o" -o "$W/$name.elf"
    run common/bin/objcopy -O binary -j .text "$W/$name.elf" "$W/text.bin"
    run common/bin/objcopy -O binary -j .data "$W/$name.elf" "$W/data.bin"
    run common/bin/objdump -t "$W/$name.elf" > "$work/syms.txt"
    python3 "$here/sdk/tools/fep_pack.py" "$work" "$slot" "$out/$name.s$slot.fep"
    echo "$out/$name.s$slot.fep"
}

if [ -n "$1" ]; then
    build "$1" "${2:-0}"
else
    build jungle 0
    build reese 1
fi
