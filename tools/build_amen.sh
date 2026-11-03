#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds the AMEN firmware (fw/: Felucca 1.9.0 with the AMEN engines, fw/AMEN.md) into build/amen/:
#   amen-VERSION.fwsc   the installable package (identity FM-1_9XY, as Felucca's release builds)
#   amen-VERSION-app.bin, amen.json (version, identity, SHA-256 for the installer site)
# Needs JIELI_TOOLCHAIN (the JieLi Linux toolchain) and AC79_SDK (a checkout of the JieLi AC79 SDK, tag
# AC79NN_SDK_V1.2.1_2023-12-13: three of its files go into the package), Python 3 with Pillow.
#   sh tools/build_amen.sh 0.2
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
ver=${1:-0.2}
out=$here/build/amen
cd "$here/fw"
python3 tools/build.py --release "$ver"
mkdir -p "$out"
cp "build/felucca-$ver.fwsc" "$out/amen-$ver.fwsc"
cp build/felucca.bin "$out/amen-$ver-app.bin"
python3 - "$out/amen-$ver.fwsc" "$ver" "$out/amen.json" <<'PY'
import hashlib, json, sys
raw = open(sys.argv[1], "rb").read()
product = "".join(chr((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF) for i in range(20) if raw[i * 0x30 + 0x2F] != 0x7D)
assert product.startswith("FM-1_9"), product
assert b"FELUCCA-LOADER-1" in raw, "no Felucca loader in the package"
json.dump({"version": sys.argv[2], "product": product, "file": "firmware/amen-%s.fwsc" % sys.argv[2],
           "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}, open(sys.argv[3], "w"))
print(sys.argv[1], len(raw), "B, identity", product)
PY
