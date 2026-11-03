#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The JUNGLE engine's drums: recorded one-shots of a real kit -> engines/jungle_drums.h

The hits are recordings from the Versilian Community Sample Library (VCSL, https://github.com/sgossner/VCSL,
CC0 1.0: public domain), at the commit below: a kick, a modern snare (a full hit and a soft one for the ghost notes),
a hi-hat closed and loose (the funk open hat) and a cymbal played with a stick (the ride; the crash is the same,
slower and louder). No breakbeat recording is used: the breaks are still the patterns of tools/gen_breaks.py,
played on these drums (eng_jungle.c), the way a drummer would play them.

Each hit: mono, from 1 ms before its onset, cut to its length with a raised-cosine fade, high-passed (DC), resampled
(windowed sinc, its own rate: the kick needs less), peak-normalised for the 8-bit code (mu-law, G.711's curve; about
13 bits of range) and given its level in the kit.

  python3 tools/gen_drums.py [VCSL_DIR]      (VCSL_DIR: a VCSL checkout at VCSL_COMMIT; else it is fetched,
                                              only these files, with git, into build/vcsl)
Needs numpy. Writes engines/jungle_drums.h and prints each hit's size.
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "engines" / "jungle_drums.h"
VCSL_URL = "https://github.com/sgossner/VCSL.git"
VCSL_COMMIT = "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e"
M, I = "Membranophones/Struck Membranophones", "Idiophones/Struck Idiophones"

# name, file, rate (Hz), length (s), fade (s), level in the kit (0..1), extra decay (s, 0 = the recording's own)
HITS = [
    ("KICK", f"{M}/Bass Drum 1/BDrumNew_hit_v7_rr1_Sum.wav", 11025, 0.30, 0.10, 1.00, 0.11),
    ("SNARE", f"{M}/Snare Drum, Modern 1/Snare2_HitSN_v9_rr1_Mid.wav", 22050, 0.24, 0.08, 0.92, 0.0),
    ("GHOST", f"{M}/Snare Drum, Modern 1/Snare2_HitSN_v3_rr1_Mid.wav", 22050, 0.12, 0.05, 0.36, 0.0),
    ("HAT", f"{I}/Hi-Hat Cymbal/HiHat_HitC_v3_rr1_Mid.wav", 22050, 0.16, 0.09, 0.42, 0.0),
    ("OHAT", f"{I}/Hi-Hat Cymbal/HiHat_HitLoose_rr1_Mid.wav", 16000, 0.26, 0.10, 0.42, 0.0),
    ("RIDE", f"{I}/Suspended Cymbal 1/susCymb1_hit_stick_f1.wav", 16000, 0.34, 0.16, 0.40, 0.0),
]


def fetch(dest):
    """only the files of HITS from VCSL at VCSL_COMMIT (a blobless clone, the blobs on checkout)"""
    if not (dest / ".git").exists():
        subprocess.run(["git", "clone", "-q", "--filter=blob:none", "--no-checkout", VCSL_URL, str(dest)], check=True)
    subprocess.run(["git", "-C", str(dest), "checkout", "-q", VCSL_COMMIT, "--", *[h[1] for h in HITS]], check=True)


def load(path):
    w = wave.open(str(path))
    sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    assert sw == 2, f"{path}: {8 * sw}-bit"
    x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64).reshape(-1, ch).mean(1)
    return x / 32768.0, sr


def resample(x, sr, to):
    """windowed sinc (Kaiser, 32 zero crossings), cut at 0.45 of the new rate"""
    n = int(len(x) * to / sr)
    t = np.arange(n) * sr / to
    fc = 0.45 * to / sr                          # in cycles per input sample
    half = int(np.ceil(32 / (2 * fc)))
    out = np.zeros(n)
    for k in range(-half, half + 1):
        idx = np.floor(t).astype(int) + k
        d = t - idx
        ok = (idx >= 0) & (idx < len(x))
        w = np.kaiser(2 * half + 1, 8.0)[np.clip(np.round(d + half).astype(int), 0, 2 * half)]
        out += np.where(ok, x[np.clip(idx, 0, len(x) - 1)], 0) * 2 * fc * np.sinc(2 * fc * d) * w
    return out


def ulaw_enc(x):
    """G.711 mu-law of x in -1..1 (14-bit linear)"""
    v = np.clip(np.round(x * 8159), -8159, 8159).astype(int)
    out = []
    for s in v:
        sign = 0x80 if s < 0 else 0
        m = min(-s if s < 0 else s, 8158) + 33
        seg = 0
        while seg < 7 and m >= (64 << seg):
            seg += 1
        out.append(~(sign | (seg << 4) | ((m >> (seg + 1)) & 15)) & 0xFF)
    return out


def ulaw_dec(c):
    c = ~c & 0xFF
    seg, man = (c >> 4) & 7, c & 15
    m = (((man << 1) + 33) << seg) - 33
    return -m if c & 0x80 else m


def hit(x, sr, rate, length, fade, decay):
    on = int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max()))
    x = x[max(0, on - sr // 1000):]
    n = int(length * sr)
    x = np.concatenate([x[:n], np.zeros(max(0, n - len(x)))])
    t = np.arange(n) / sr
    if decay:
        x = x * np.where(t < 0.03, 1.0, np.exp(-(t - 0.03) / decay))
    nf = int(fade * sr)
    x[n - nf:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, nf))
    hp = np.zeros_like(x)                         # a 20 Hz high-pass: no DC under the codes
    a, prev_x, prev_y = np.exp(-2 * np.pi * 20 / sr), 0.0, 0.0
    for i, v in enumerate(x):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        hp[i] = prev_y
    y = resample(hp, sr, rate)
    return y / np.abs(y).max() * 0.98


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "vcsl"
    if len(sys.argv) <= 1:
        src.parent.mkdir(parents=True, exist_ok=True)
        fetch(src)
    lines = ["/* generated by tools/gen_drums.py: the JUNGLE engine's drums, recordings of a real kit from the",
             f" * Versilian Community Sample Library (CC0 1.0, https://github.com/sgossner/VCSL at {VCSL_COMMIT[:12]}):",
             " * 8-bit mu-law, each at its own rate (r16: rate * 65536 / 44100), level Q15 */",
             "#pragma once", "#include <stdint.h>", "",
             "static const int16_t JNG_ULAW[256] = {"]
    tab = [ulaw_dec(c) * 4 for c in range(256)]   # (14-bit -> 16-bit)
    for i in range(0, 256, 16):
        lines.append("    " + ", ".join(str(v) for v in tab[i:i + 16]) + ",")
    lines.append("};")
    total, rows = 0, []
    for name, f, rate, length, fade, level, decay in HITS:
        x, sr = load(src / f)
        y = hit(x, sr, rate, length, fade, decay)
        codes = ulaw_enc(y)
        err = np.sqrt(np.mean((np.array([tab[c] for c in codes]) / 32768.0 - y * 8159 * 4 / 32768.0) ** 2))
        lines.append(f"static const uint8_t JNG_D_{name}[{len(codes)}] = {{   /* {f.split('/')[-1]} */")
        for i in range(0, len(codes), 24):
            lines.append("    " + ", ".join(str(c) for c in codes[i:i + 24]) + ",")
        lines.append("};")
        rows.append(f"    {{JNG_D_{name}, {len(codes)}u, {rate * 65536 // 44100}u, {int(level * 32767)}}},   /* {name} {rate} Hz */")
        total += len(codes)
        print(f"{name:6s} {rate:5d} Hz {len(codes) / rate:.2f} s {len(codes):6d} B  (code rms error {err:.4f})")
    lines += ["", "typedef struct { const uint8_t *d; uint16_t n, r16; int16_t level; } jng_drum_t;",
              "enum { JD_KICK, JD_SNARE, JD_GHOST, JD_HAT, JD_OHAT, JD_RIDE, JD_N };",
              "static const jng_drum_t JNG_DRUM[JD_N] = {"] + rows + ["};", ""]
    OUT.write_text("\n".join(lines))
    print(f"drums {total} B + table 512 B -> {OUT}")


if __name__ == "__main__":
    main()
