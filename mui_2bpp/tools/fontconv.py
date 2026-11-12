#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
"""
fontconv.py: a 2 bpp anti-aliased bitmap font for mui_2bpp, with only the
characters that are needed.

The characters: those in a UTF-8 text file (made by cstrings.py; line ends
and other control characters are ignored), plus the code point ranges of
-m. FreeType renders each glyph through Pillow, hinted and anti-aliased,
and the coverage is rounded to 4 levels:

  0 background, 1 a third, 2 two thirds, 3 foreground

the pixel codes of the 2 bpp frame buffer. Characters missing from the font
are reported and left out.

Output: NAME.c with the font, one const uint8_t array, and NAME.h with its
declaration. With -p, a picture of all glyphs, decoded from the array.

Font format, multi-byte fields little-endian:

  header, 8 bytes
    0  format: 1, raw glyphs at 2 bits per pixel
    1  pixel size
    2  ascent: pixels above the baseline: -a, or the largest of the font's
       ascender (OS/2 typo) and the tops of the glyphs
    3  descent: pixels below the baseline: -d, or the largest of the font's
       descender and the bottoms of the glyphs
       mui draws the bar of a focused field ascent + descent + 2 pixels
       high, from ascent + 1 above the baseline: with -a 24 -d 6, 32 pixels
    4  the largest advance
    5  0
    6  glyph count, 16 bits
  index, 5 bytes per glyph, in code point order, for a binary search
    0  code point, 16 bits
    2  offset of the glyph from the start of the font, 24 bits
  glyph
    0  w, the width of the bitmap; 0 for a glyph without pixels (space)
    1  h, the height
    2  x, signed: the left of the bitmap, from the pen position
    3  y, signed: the top of the bitmap, in rows above the baseline
    4  advance: from this pen position to the next
    5  the pixels, row by row, 4 to a byte, the leftmost in bits 7:6; rows
       not padded: (w * h + 3) / 4 bytes

The glyphs are derived from the font and stay under its license: the
comment of the output names the font, its copyright and, with -L, the
license (e.g. for the SIL Open Font License: a Modified Version).

usage: fontconv.py [-v] -s SIZE [-a ASCENT -d DESCENT] [-m RANGES] [-f FACE] [-L LICENSE]
                   -n NAME -o NAME.c [-p overview.png] font.otf [chars.txt]
"""

import argparse
import os
import sys

from fontTools.ttLib import TTCollection, TTFont
from PIL import Image, ImageDraw, ImageFont

FORMAT_RAW2 = 1
HEADER = 8
ENTRY = 5


def ranges(spec):
    """'32-126,0x3000-0x303f,$ff01': a set of code points"""
    out = set()
    for part in filter(None, (p.strip() for p in spec.split(","))):
        lo, _, hi = part.partition("-")
        lo, hi = num(lo), num(hi) if hi else num(lo)
        out.update(range(lo, hi + 1))
    return out


def num(s):
    s = s.strip()
    if s.startswith("$"):
        return int(s[1:], 16)
    return int(s, 0)


def face(path, name):
    """the fontTools font and the face index of path; in a collection the
    face with full name 'name'"""
    with open(path, "rb") as f:
        collection = f.read(4) == b"ttcf"
    if not collection:
        tt = TTFont(path)
        if name and tt["name"].getDebugName(4) != name:
            sys.exit(f"fontconv.py: {path} is {tt['name'].getDebugName(4)}, not {name}")
        return tt, 0
    fonts = TTCollection(path).fonts
    names = [tt["name"].getDebugName(4) for tt in fonts]
    if name in names:
        return fonts[names.index(name)], names.index(name)
    sys.exit(f"fontconv.py: {path} is a collection, -f one of: " + ", ".join(names))


def render(font, ch, size):
    """the glyph of ch: (w, h, x, y, advance, levels row by row)"""
    ox, oy = 2 * size, 3 * size                # the pen, on the baseline
    img = Image.new("L", (5 * size, 5 * size), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = "L"
    d.text((ox, oy), ch, font=font, fill=255, anchor="ls")
    adv = round(font.getlength(ch))
    bb = img.getbbox()
    if not bb:
        return 0, 0, 0, 0, adv, []
    levels = [(g * 3 + 127) // 255 for g in img.crop(bb).tobytes()]
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    if not any(levels):                        # coverage that rounds to 0
        return 0, 0, 0, 0, adv, []
    return w, h, bb[0] - ox, oy - bb[1], adv, levels


def pack(levels):
    """4 pixels to a byte, the first in bits 7:6"""
    out = bytearray((len(levels) + 3) // 4)
    for i, v in enumerate(levels):
        out[i // 4] |= v << (6 - 2 * (i % 4))
    return out


def build(glyphs, size, ascent, descent):
    """the font as bytes; glyphs: {code point: (w, h, x, y, adv, levels)}"""
    cps = sorted(glyphs)
    body, offsets = bytearray(), []
    start = HEADER + ENTRY * len(cps)
    for cp in cps:
        w, h, x, y, adv, levels = glyphs[cp]
        offsets.append(start + len(body))
        body += bytes([w, h, x & 0xFF, y & 0xFF, adv]) + pack(levels)
    maxadv = max((g[4] for g in glyphs.values()), default=0)
    out = bytearray([FORMAT_RAW2, size, ascent, descent, maxadv, 0,
                     len(cps) & 0xFF, len(cps) >> 8])
    for cp, off in zip(cps, offsets):
        if off >= 1 << 24:
            sys.exit("fontconv.py: font larger than 16 MB")
        out += bytes([cp & 0xFF, cp >> 8, off & 0xFF, (off >> 8) & 0xFF, off >> 16])
    return out + body


def decode(font):
    """the glyphs of a font in this format, read back: the check of build()
    and the source of the overview picture"""
    assert font[0] == FORMAT_RAW2
    count = font[6] | font[7] << 8
    out = {}
    for k in range(count):
        e = HEADER + ENTRY * k
        cp = font[e] | font[e + 1] << 8
        off = font[e + 2] | font[e + 3] << 8 | font[e + 4] << 16
        w, h, x, y, adv = font[off:off + 5]
        x, y = x - 256 * (x > 127), y - 256 * (y > 127)
        px = font[off + 5:off + 5 + (w * h + 3) // 4]
        levels = [(px[i // 4] >> (6 - 2 * (i % 4))) & 3 for i in range(w * h)]
        out[cp] = (w, h, x, y, adv, levels)
    return out


def overview(font, path):
    """all glyphs in a grid, twice the size: the baseline and the ascent and
    descent of the header in blue, the advance in red"""
    glyphs = decode(font)
    asc, desc, maxadv = font[2], font[3], font[4]
    cw, ch, cols = maxadv + 8, asc + desc + 8, 16
    rows = (len(glyphs) + cols - 1) // cols
    img = Image.new("RGB", (cols * cw, rows * ch), (0, 0, 0))
    gray = [(0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255)]
    for k, cp in enumerate(sorted(glyphs)):
        w, h, x, y, adv, levels = glyphs[cp]
        x0, base = (k % cols) * cw + 4, (k // cols) * ch + 4 + asc
        for yy in (base - asc, base, base + desc - 1):
            for xx in range(x0, x0 + adv):
                img.putpixel((xx, yy), (0, 0, 96))
        for yy in range(base - asc, base + desc):
            img.putpixel((x0 + adv, yy), (96, 0, 0))
        for i, v in enumerate(levels):
            if v:
                img.putpixel((x0 + x + i % w, base - y + i // w), gray[v])
    img.resize((2 * img.width, 2 * img.height), Image.NEAREST).save(path)


def c_char(cp):
    c = chr(cp)
    return "" if c in "\\" or (0xD800 <= cp < 0xE000) else c.replace("*/", "*\\/")


def write_c(path, name, font, glyphs, info):
    count = len(glyphs)
    with open(path, "w", encoding="utf-8") as f:
        f.write("/*\n")
        for line in info:
            f.write(f"  {line}\n")
        f.write("*/\n\n")
        f.write(f'#include "{name}.h"\n\n')
        f.write(f"const uint8_t {name}[{len(font)}] = {{\n")
        f.write("  /* format, size, ascent, descent, max advance, 0, glyph count */\n")
        f.write("  " + ", ".join(f"0x{b:02x}" for b in font[:HEADER]) + ",\n")
        f.write("  /* index: code point, offset */\n")
        cps = sorted(glyphs)
        for k, cp in enumerate(cps):
            e = HEADER + ENTRY * k
            f.write("  " + ", ".join(f"0x{b:02x}" for b in font[e:e + ENTRY])
                    + f",  /* U+{cp:04X} {c_char(cp)} */\n")
        for k, cp in enumerate(cps):
            e = HEADER + ENTRY * k
            off = font[e + 2] | font[e + 3] << 8 | font[e + 4] << 16
            end = len(font) if k + 1 == count else \
                font[e + 7] | font[e + 8] << 8 | font[e + 9] << 16
            w, h, x, y, adv, _ = glyphs[cp]
            f.write(f"  /* U+{cp:04X} {c_char(cp)}  {w}x{h} x {x} y {y} advance {adv} */\n")
            data = font[off:end]
            for i in range(0, len(data), 16):
                f.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
        f.write("};\n")


def write_h(path, name, size):
    guard = name.upper() + "_H"
    with open(path, "w", encoding="utf-8") as f:
        f.write("/* generated by fontconv.py: a 2 bpp anti-aliased font for mui_2bpp */\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n")
        f.write(f"extern const uint8_t {name}[{size}];\n\n#endif\n")


def main():
    ap = argparse.ArgumentParser(description="2 bpp anti-aliased bitmap font for mui_2bpp")
    ap.add_argument("-s", type=int, required=True, metavar="SIZE", help="pixel size (the em)")
    ap.add_argument("-a", type=int, metavar="ASCENT", help="ascent of the header, in pixels")
    ap.add_argument("-d", type=int, metavar="DESCENT", help="descent of the header, in pixels")
    ap.add_argument("-m", default="", metavar="RANGES",
                    help="code points always included, e.g. '32-126' (decimal, 0x or $ hex)")
    ap.add_argument("-f", metavar="FACE", help="full name of the face, e.g. 'Noto Sans CJK SC'; "
                    "required for a collection (.ttc)")
    ap.add_argument("-n", required=True, metavar="NAME", help="C identifier of the font")
    ap.add_argument("-o", required=True, metavar="NAME.c", help="output; NAME.h beside it")
    ap.add_argument("-p", metavar="FILE.png", help="overview picture of the glyphs")
    ap.add_argument("-L", metavar="TEXT", help="the license, for the comment of the output "
                    "(default: the license URL of the font)")
    ap.add_argument("-v", action="store_true", help="log to standard error")
    ap.add_argument("font", help=".otf, .ttf or .ttc")
    ap.add_argument("chars", nargs="?", help="UTF-8 text file with the characters (cstrings.py)")
    a = ap.parse_args()

    want = ranges(a.m)
    if a.chars:
        with open(a.chars, encoding="utf-8") as f:
            want |= {ord(c) for c in f.read() if ord(c) >= 0x20 and ord(c) != 0x7F}
    big = sorted(cp for cp in want if cp > 0xFFFF)
    if big:
        print("fontconv.py: left out, beyond 16 bits: " + "".join(map(chr, big)), file=sys.stderr)
    want -= set(big)

    tt, index = face(a.font, a.f)
    cmap = tt.getBestCmap()
    missing = sorted(cp for cp in want if cp not in cmap)
    if missing:
        print("fontconv.py: not in the font, left out: " + "".join(map(chr, missing)), file=sys.stderr)
    want -= set(missing)

    upem = tt["head"].unitsPerEm
    os2 = tt["OS/2"]
    asc = round(os2.sTypoAscender * a.s / upem)
    desc = round(-os2.sTypoDescender * a.s / upem)

    pil = ImageFont.truetype(a.font, a.s, index=index, layout_engine=ImageFont.Layout.BASIC)
    glyphs = {}
    for cp in sorted(want):
        g = glyphs[cp] = render(pil, chr(cp), a.s)
        w, h, x, y, adv = g[:5]
        if not (w < 256 and h < 256 and -128 <= x < 128 and -128 <= y < 128 and adv < 256):
            sys.exit(f"fontconv.py: U+{cp:04X} too large for the format")
    ascent = max([asc] + [g[3] for g in glyphs.values() if g[0]]) if a.a is None else a.a
    descent = max([desc] + [g[1] - g[3] for g in glyphs.values() if g[0]]) if a.d is None else a.d
    if not (0 <= ascent < 128 and 0 <= descent < 128):
        sys.exit("fontconv.py: ascent and descent 0..127")
    over = sorted(cp for cp, g in glyphs.items() if g[0] and (g[3] > ascent or g[1] - g[3] > descent))
    if over:
        print("fontconv.py: beyond the ascent or descent: " + "".join(map(chr, over)), file=sys.stderr)

    font = build(glyphs, a.s, ascent, descent)
    assert decode(font) == glyphs

    n = tt["name"]
    stem = os.path.splitext(a.o)[0]
    info = [
        f"{a.n}: generated by fontconv.py, a 2 bpp anti-aliased font for mui_2bpp.",
        "command: fontconv.py " + " ".join(sys.argv[1:]),
        f"font: {n.getDebugName(4)}, {n.getDebugName(5)}",
        f"copyright: {n.getDebugName(0)}",
        "license: " + (a.L or n.getDebugName(14) or "that of the font"),
        f"size {a.s} pixels, ascent {ascent}, descent {descent}, {len(glyphs)} glyphs, {len(font)} bytes",
    ]
    write_c(stem + ".c", a.n, font, glyphs, info)
    write_h(stem + ".h", a.n, len(font))
    if a.p:
        overview(font, a.p)

    if a.v:
        other = sum(cp >= 0x80 for cp in glyphs)
        print(f"{a.n}: {len(glyphs)} glyphs ({other} not ASCII), {len(font)} bytes; "
              f"ascent {ascent} descent {descent} (font {asc} {desc}), max advance {font[4]}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
