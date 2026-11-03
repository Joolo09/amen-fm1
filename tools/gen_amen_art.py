#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""AMEN's splash art: an original 80s anime DJ (drawn here, from shapes: not traced from anyone's picture) and the
chrome AMEN logo (Orbitron, SIL OFL 1.1, fw/assets/fonts/OFL-Orbitron.txt), as run-length sprites for the firmware.

  python3 tools/gen_amen_art.py fw/firmware/src/amen_art.h [--preview PNG]

Both are drawn 4x over size, brought down to the screen's pixels, their edges cut hard at half cover (the background
behind them is drawn by the firmware: the sky, the sun, the grid; ui_draw.c draw_splash) and their colours snapped to
one shared palette of 31 colours (RGB565) + transparent.

Sprite data: rows top to bottom, each byte (index << 3) | (run - 1): a run of 1..8 pixels of one palette index;
index 0 is transparent (the background stays). --preview draws the splash as the firmware does, for a look.
"""
import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 4                                               # drawn at 4x
ROOT = Path(__file__).resolve().parents[1]
ORBITRON = ROOT / "fw" / "assets" / "fonts" / "Orbitron[wght].ttf"

# ------------------------------------------------------------------------------------------- the palette ---
PAL = {
    "line": (34, 8, 46),        # the ink outline
    "skin": (255, 222, 206), "skin2": (240, 176, 172), "skin3": (214, 128, 150),
    "blush": (255, 128, 160), "lip": (232, 70, 120), "white": (255, 255, 255),
    "hair0": (40, 22, 96), "hair1": (72, 40, 150), "hair2": (122, 82, 214), "rim": (255, 150, 90), "rim2": (255, 96, 170),
    "iris0": (48, 16, 96), "iris1": (150, 40, 190), "iris2": (255, 92, 214), "iris3": (255, 196, 240),
    "phone0": (60, 60, 92), "phone1": (200, 206, 230), "phone2": (40, 228, 255),
    "jacket0": (14, 92, 150), "jacket1": (24, 170, 220), "jacket2": (120, 236, 255),
    "top0": (30, 14, 50), "top1": (64, 30, 96), "gold": (255, 200, 60),
    "chrome0": (190, 236, 255), "chrome1": (90, 140, 230), "chrome2": (60, 20, 90), "chrome3": (255, 120, 60),
    "chrome4": (255, 220, 120),
}
NAMES = list(PAL)
assert len(NAMES) <= 31, len(NAMES)


def to565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ----------------------------------------------------------------------------------------------- shapes ---
def spline(pts, n=10, closed=True):
    """a closed (or open) Catmull-Rom curve through pts (screen coordinates), as polygon points at 4x"""
    out = []
    m = len(pts)
    rng = range(m) if closed else range(m - 1)
    for i in rng:
        p0 = pts[(i - 1) % m] if closed or i > 0 else pts[0]
        p1, p2 = pts[i], pts[(i + 1) % m]
        p3 = pts[(i + 2) % m] if closed or i + 2 < m else pts[-1]
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 +
                       (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 +
                       (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x * S, y * S))
    if not closed:
        out.append((pts[-1][0] * S, pts[-1][1] * S))
    return out


class Pen:
    def __init__(self, w=240, h=240):
        self.im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def shape(self, pts, fill, line=True, lw=1.0, n=10):
        poly = spline(pts, n)
        self.d.polygon(poly, fill=PAL[fill] + (255,))
        if line:
            self.d.line(poly + [poly[0]], fill=PAL["line"] + (255,), width=max(1, round(lw * S)), joint="curve")

    def poly(self, pts, fill, line=False, lw=1.0):
        p = [(x * S, y * S) for x, y in pts]
        self.d.polygon(p, fill=PAL[fill] + (255,))
        if line:
            self.d.line(p + [p[0]], fill=PAL["line"] + (255,), width=max(1, round(lw * S)), joint="curve")

    def curve(self, pts, col, lw=1.0, n=10):
        self.d.line(spline(pts, n, closed=False), fill=PAL[col] + (255,), width=max(1, round(lw * S)), joint="curve")

    def ell(self, cx, cy, rx, ry, fill, line=False, lw=1.0):
        box = [(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S]
        self.d.ellipse(box, fill=PAL[fill] + (255,), outline=PAL["line"] + (255,) if line else None,
                       width=max(1, round(lw * S)) if line else 0)

    def clip_paint(self, mask_pen_fn, paint_fn):
        """paint_fn's strokes kept only inside the shape mask_pen_fn draws (for shading and rim light)"""
        layer = Pen(self.im.width // S, self.im.height // S)
        paint_fn(layer)
        mask = Pen(self.im.width // S, self.im.height // S)
        mask_pen_fn(mask)
        a = mask.im.split()[3]
        layer.im.putalpha(Image.composite(layer.im.split()[3], Image.new("L", a.size, 0), a))
        self.im.alpha_composite(layer.im)


# ------------------------------------------------------------------------------------------------ the DJ ---
# Screen coordinates (240 x 240). She stands at the right, the sun behind her left shoulder, a three-quarter turn to
# the left: the face's centre line at x 168, the far eye a little narrower.
HAIR_BACK = [(124, 64), (132, 36), (152, 20), (178, 16), (204, 24), (222, 44), (230, 74), (232, 110), (236, 150),
             (238, 196), (226, 200), (214, 176), (128, 176), (116, 196), (106, 186), (114, 150), (118, 110)]
FACE = [(140, 70), (196, 70), (199, 94), (194, 112), (184, 126), (171, 135), (160, 128), (148, 114), (141, 96)]
BANGS = [(130, 74), (134, 50), (150, 32), (174, 26), (198, 34), (212, 52), (214, 76), (206, 70), (204, 86), (196, 72),
         (190, 84), (184, 70), (176, 86), (170, 70), (162, 88), (156, 70), (148, 90), (144, 72), (136, 92)]


def draw_dj(p):
    # long hair behind: falls behind the shoulders
    p.shape(HAIR_BACK, "hair0", n=12)
    p.clip_paint(lambda m: m.shape(HAIR_BACK, "hair0", line=False, n=12), lambda l: (
        l.curve([(118, 112), (114, 150), (108, 186)], "rim2", lw=3.0),                          # the sun's rim light
        l.curve([(214, 60), (226, 100), (230, 150), (232, 194)], "hair1", lw=4),
        l.curve([(126, 66), (120, 100), (117, 130)], "rim", lw=2.2)))
    # the jacket: big 80s shoulders, the collar up
    jacket = [(100, 240), (104, 196), (112, 172), (132, 160), (156, 152), (186, 152), (212, 160), (232, 174),
              (240, 194), (240, 240)]
    p.shape(jacket, "jacket1", n=8)
    p.clip_paint(lambda m: m.shape(jacket, "jacket1", line=False, n=8), lambda l: (
        l.poly([(104, 200), (120, 168), (140, 160), (128, 240), (100, 240)], "jacket0"),
        l.poly([(200, 156), (230, 170), (240, 190), (240, 240), (222, 240)], "jacket0"),
        l.curve([(116, 176), (134, 164), (154, 158)], "jacket2", lw=2),
        l.curve([(190, 158), (214, 164), (232, 178)], "jacket2", lw=2)))
    # the top under it, and the neck
    p.shape([(146, 150), (194, 150), (190, 240), (150, 240)], "top0", n=4)
    p.poly([(156, 150), (184, 150), (170, 176)], "top1")
    p.shape([(160, 124), (182, 124), (184, 152), (170, 168), (156, 152)], "skin", n=6)
    p.poly([(160, 128), (182, 128), (180, 140), (170, 146), (161, 140)], "skin2")              # the chin's shadow
    p.d.rectangle([158 * S, 148 * S, 184 * S, 151 * S], fill=PAL["line"] + (255,))           # the choker
    p.ell(171, 153, 2.2, 2.2, "gold")
    # the collar (popped), over the neck
    p.poly([(140, 138), (158, 146), (166, 182), (150, 200), (134, 170)], "jacket2", line=True, lw=1.0)
    p.poly([(200, 138), (182, 146), (176, 182), (192, 200), (208, 170)], "jacket1", line=True, lw=1.0)
    p.curve([(146, 150), (152, 172)], "jacket1", lw=1.2)
    p.curve([(194, 150), (190, 172)], "jacket0", lw=1.2)
    # a lightning badge on the near lapel
    p.poly([(124, 196), (132, 196), (128, 203), (134, 203), (122, 216), (125, 206), (120, 206)], "gold", line=True, lw=0.8)
    # the face
    p.shape(FACE, "skin", n=10)
    p.clip_paint(lambda m: m.shape(FACE, "skin", line=False, n=10), lambda l: (
        l.shape([(186, 66), (204, 70), (202, 110), (186, 132), (190, 104)], "skin2", line=False),   # the far side shade
        l.shape([(140, 66), (160, 66), (152, 84), (140, 86)], "skin2", line=False),
        l.shape([(140, 70), (200, 70), (200, 84), (190, 90), (176, 88), (164, 92), (150, 92), (140, 88)], "skin2", line=False)))
    # the eyes: 80s anime, tall, a big light and a small one; the far one (right) a little narrower
    for cx, rx, lift in ((156, 9.0, 0), (186, 7.5, 1)):
        cy = 97 - lift
        p.ell(cx, cy, rx, 8.5, "white")
        p.ell(cx + 1, cy + 1, rx * 0.72, 7.6, "iris0")
        p.clip_paint(lambda m, cx=cx, cy=cy, rx=rx: m.ell(cx + 1, cy + 1, rx * 0.72, 7.6, "iris0"), lambda l, cx=cx, cy=cy, rx=rx: (
            l.ell(cx + 1, cy + 6, rx * 0.8, 5.5, "iris1"),
            l.ell(cx + 1, cy + 8, rx * 0.6, 3.2, "iris2")))
        p.ell(cx + 1, cy + 1, rx * 0.30, 3.6, "line")                                       # the pupil
        p.ell(cx - rx * 0.35, cy - 3, rx * 0.30, 2.8, "white")                              # the big light
        p.ell(cx + rx * 0.4, cy + 4.5, 1.1, 1.1, "iris3")                                   # the small one
        # the lashes: a heavy upper line with an outer flick, a fine lower line
        side = -1 if cx < 170 else 1
        outer = cx + side * (rx + 3)
        p.curve([(cx - side * (rx + 0.5), cy - 3), (cx - side * rx * 0.2, cy - 9.5), (cx + side * rx * 0.6, cy - 9),
                 (outer, cy - 6)], "line", lw=2.6)
        p.curve([(outer - side * 2, cy - 6.5), (outer + side * 1.5, cy - 9)], "line", lw=1.6)
        p.curve([(cx - side * rx * 0.6, cy + 8.5), (cx + side * rx * 0.7, cy + 8.2)], "skin3", lw=1.0)
        # the brow, thin, high
        p.curve([(cx - side * 6, cy - 15), (cx + side * 1, cy - 17), (cx + side * 8, cy - 15.5)], "hair0", lw=1.4)
    # the nose, the mouth, the blush
    p.curve([(170, 108), (171.5, 112), (169.5, 113)], "skin3", lw=1.2)
    p.shape([(163, 121), (172, 120), (177, 119), (172, 125), (166, 124)], "lip", lw=0.8, n=6)
    p.curve([(164, 121.5), (171, 121.5), (176, 120)], "line", lw=1.0)
    for bx in (150, 189):
        for k in range(3):
            p.curve([(bx + k * 3, 112), (bx + k * 3 - 2, 115)], "blush", lw=1.0)
    # the bangs over the brow, a light on them
    p.shape(BANGS, "hair1", n=8)
    p.clip_paint(lambda m: m.shape(BANGS, "hair1", line=False, n=8), lambda l: (
        l.curve([(150, 40), (172, 32), (196, 40)], "hair2", lw=3.5),
        l.curve([(158, 52), (164, 72)], "hair2", lw=1.5), l.curve([(184, 50), (188, 70)], "hair2", lw=1.5),
        l.curve([(131, 80), (133, 58), (142, 42), (154, 32)], "rim", lw=2.2)))
    # the headphones: the band over the hair, the cups over the ears, a cyan glow ring
    p.curve([(134, 84), (132, 48), (150, 22), (174, 16), (200, 22), (216, 46), (214, 84)], "line", lw=6.5)
    p.curve([(134, 84), (132, 48), (150, 22), (174, 16), (200, 22), (216, 46), (214, 84)], "phone1", lw=4.0)
    for cx, w in ((134, 1.0), (213, 0.8)):
        p.ell(cx, 96, 10 * w, 15, "phone0", line=True, lw=1.2)
        p.ell(cx, 96, 7 * w, 11, "phone1")
        p.ell(cx, 96, 4.5 * w, 7.5, "phone2")
        p.ell(cx, 96, 2.5 * w, 4.5, "phone0")
    # a gold hoop under the near cup
    p.d.ellipse([127 * S, 110 * S, 137 * S, 124 * S], outline=PAL["gold"] + (255,), width=round(1.4 * S))


# -------------------------------------------------------------------------------------------- the logo ---
def draw_logo(w=124, h=46):
    """AMEN in Orbitron Black, slanted, chrome: sky above the middle line, a sunset below, a dark outline and a pink
    glow; at 4x, its own canvas"""
    f = ImageFont.truetype(str(ORBITRON), 38 * S)
    try:
        f.set_variation_by_axes([900])
    except Exception:
        pass
    text = "AMEN"
    bb = f.getbbox(text)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    mask = Image.new("L", (tw + 8 * S, th + 8 * S), 0)
    ImageDraw.Draw(mask).text((4 * S - bb[0], 4 * S - bb[1]), text, font=f, fill=255)
    sh = 0.22                                                                  # the slant
    mw = mask.width + int(mask.height * sh)
    mask = mask.transform((mw, mask.height), Image.AFFINE, (1, sh, -mask.height * sh, 0, 1, 0), Image.BICUBIC)
    mask = mask.resize((w * S, h * S), Image.LANCZOS)
    top, bot = 4 * S, h * S - 4 * S
    mid = top + (bot - top) * 0.52
    grad = Image.new("RGBA", mask.size)
    gd = ImageDraw.Draw(grad)
    for y in range(mask.height):
        if y < mid:
            t = max(0.0, (y - top) / (mid - top))
            c = PAL["chrome0"] if t < 0.45 else PAL["white"] if t < 0.62 else PAL["chrome1"]
        elif y < mid + 1.2 * S:
            c = PAL["chrome2"]
        else:
            t = (y - mid) / (bot - mid)
            c = PAL["chrome3"] if t < 0.55 else PAL["chrome4"]
        gd.line([(0, y), (mask.width, y)], fill=c + (255,))
    out = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    glow = mask.filter(ImageFilter.MaxFilter(2 * S + 1))
    out.paste(Image.new("RGBA", mask.size, PAL["iris2"] + (255,)), (0, 0), glow.filter(ImageFilter.MaxFilter(S + 1)))
    out.paste(Image.new("RGBA", mask.size, PAL["line"] + (255,)), (0, 0), glow)
    out.paste(grad, (0, 0), mask)
    return out


# ------------------------------------------------------------------------------------- down to the pixels ---
def to_sprite(im4):
    """4x RGBA -> (w, h, bbox x0, y0, indices) on the screen's pixels: alpha cut at half, colours to the palette"""
    small = im4.resize((im4.width // S, im4.height // S), Image.BOX)
    pal = [PAL[n] for n in NAMES]
    px = small.load()
    w, h = small.size
    idx = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            r, g, b = (r * 255 // a, g * 255 // a, b * 255 // a) if a < 255 else (r, g, b)
            best = min(range(len(pal)), key=lambda i: (pal[i][0] - r) ** 2 * 3 + (pal[i][1] - g) ** 2 * 4 + (pal[i][2] - b) ** 2 * 2)
            idx[y][x] = best + 1
    ys = [y for y in range(h) if any(idx[y])]
    xs = [x for x in range(w) if any(idx[y][x] for y in range(h))]
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    return x1 - x0, y1 - y0, x0, y0, [row[x0:x1] for row in idx[y0:y1]]


def rle(rows):
    out = bytearray()
    for row in rows:                                   # runs do not cross rows (a band starts on a row)
        x = 0
        while x < len(row):
            v, n = row[x], 1
            while x + n < len(row) and row[x + n] == v and n < 8:
                n += 1
            out.append((v << 3) | (n - 1))
            x += n
    return bytes(out)


def unrle(data, w, h):
    rows, i = [], 0
    for _ in range(h):
        row = []
        while len(row) < w:
            b = data[i]
            i += 1
            row += [b >> 3] * ((b & 7) + 1)
        rows.append(row)
    return rows


# ------------------------------------------------------------------------- the firmware's background, here ---
def background():
    """ui_draw.c's splash background (sky, stars, the sun, the grid), the same maths, for the preview"""
    im = Image.new("RGB", (240, 240))
    px = im.load()
    for y in range(240):
        for x in range(240):
            px[x, y] = splash_bg(x, y)
    return im


SKY = [(0, (10, 4, 28)), (60, (42, 8, 82)), (112, (150, 30, 140)), (140, (255, 100, 110)), (150, (255, 170, 80))]
HORIZON, SUN_X, SUN_Y, SUN_R = 150, 92, 120, 50


def tdiv(a, b):
    """C's integer division (toward zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def mix(a, b, n, d):
    return tuple(a[i] + tdiv((b[i] - a[i]) * n, d) for i in range(3))


def splash_bg(x, y):
    """ui_draw.c spl_bg, the same integer maths"""
    d = y - HORIZON
    if d < 0:
        dx, dy = x - SUN_X, y - SUN_Y
        if dx * dx + dy * dy <= SUN_R * SUN_R:
            c, t = y - (SUN_Y - 10), (y - (SUN_Y - SUN_R)) * 13
            if not (c > 0 and c % 7 < 1 + c // 9):
                return mix((255, 236, 90), (255, 60, 150), min(t, 2 * SUN_R * 10), 2 * SUN_R * 10)
        if (x * 37 + y * 101) % 211 == 0 and y < 100:
            return (255, 255, 255)
        k = 1
        while y >= SKY[k][0]:
            k += 1
        return mix(SKY[k - 1][1], SKY[k][1], y - SKY[k - 1][0], SKY[k][0] - SKY[k - 1][0])
    if d == 0:
        return (255, 120, 210)
    if d > 1 and ((2400 // d) // 60 != (2400 // (d + 1)) // 60 or ((x - 120) * 3) // (d + 2) != ((x - 119) * 3) // (d + 2)):
        return mix((150, 40, 150), (255, 60, 200), min(d, 40), 40)
    return mix((44, 8, 74), (16, 4, 34), min(d, 90), 90)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--preview")
    a = ap.parse_args()
    pen = Pen()
    draw_dj(pen)
    dj = to_sprite(pen.im)
    logo = to_sprite(draw_logo())
    lines = ["/* generated by tools/gen_amen_art.py: AMEN's splash art, an original 80s anime DJ and the chrome logo",
             " * (Orbitron, SIL OFL 1.1, Copyright 2018 The Orbitron Project Authors; fw/assets/fonts/OFL-Orbitron.txt).",
             " * Sprites: rows top down, each byte (index << 3) | (run - 1); index 0 transparent, i: ART_PAL[i - 1] */",
             "#pragma once", "#include <stdint.h>", "",
             "static const uint16_t ART_PAL[%d] = {" % len(NAMES) + ", ".join(f"0x{to565(PAL[n]):04x}" for n in NAMES) + "};"]
    total = 0
    for name, (w, h, x0, y0, rows) in (("DJ", dj), ("LOGO", logo)):
        data = rle(rows)
        assert unrle(data, w, h) == rows
        total += len(data)
        lines.append(f"#define ART_{name}_W {w}\n#define ART_{name}_H {h}\n#define ART_{name}_X {x0}   /* (as drawn: on the screen / in the logo's box) */\n#define ART_{name}_Y {y0}")
        lines.append(f"static const uint8_t ART_{name}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
        lines.append("};")
        print(f"{name}: {w} x {h} at ({x0}, {y0}), {len(data)} B")
    Path(a.out).write_text("\n".join(lines) + "\n")
    print(f"art total {total} B + palette {2 * len(NAMES)} B -> {a.out}")
    if a.preview:
        bg = background().convert("RGBA")
        for (w, h, x0, y0, rows), at in ((dj, (0, 0)), (logo, (LOGO_AT[0], LOGO_AT[1]))):
            for y in range(h):
                for x in range(w):
                    v = rows[y][x]
                    if v:
                        bg.putpixel((at[0] + x0 + x, at[1] + y0 + y), PAL[NAMES[v - 1]] + (255,))
        bg.convert("RGB").resize((720, 720), Image.NEAREST).save(a.preview)


LOGO_AT = (2, 12)                                    # the logo's box on the screen (ui_draw.c draw_splash)

if __name__ == "__main__":
    main()
