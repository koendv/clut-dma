#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
"""
check_pattern.py: compares the display of pattern.c, as st7789.py draws it,
with the colors computed from the bitmap, tile map and clut by the formula
of clut_dma, 2 bits per pixel:

  t    = (y/8) * TILE_COLS + x/8
  idx  = map nibble t
  code = (FB[y*LINE_STRIDE + x/4] >> (6 - 2*(x%4))) & 3
  word = CLUT[2*idx + (code >> 1)]
  pix  = (code & 1) ? word[15:0] : word[31:16]

The 40 rows of the 240x280 panel below the screen: black.

usage: check_pattern.py dump.bin frame.png
"""

import struct
import sys

from PIL import Image

W, H, PANEL_H, SCALE = 240, 240, 280, 2
COLS, STRIDE = W // 8, W // 4


def ext4(v):
    return v << 4 | v


def main():
    dump = open(sys.argv[1], "rb").read()
    fb = dump[:STRIDE * H]
    tmap = dump[STRIDE * H:STRIDE * H + (COLS * (H // 8) + 1) // 2]
    clut = struct.unpack("<32I", dump[-128:])
    img = Image.open(sys.argv[2]).convert("RGB")
    assert img.size == (W * SCALE, PANEL_H * SCALE), img.size

    bad = 0
    for y in range(PANEL_H):
        for x in range(W):
            if y < H:
                t = (y // 8) * COLS + x // 8
                idx = (tmap[t // 2] >> (4 * (t & 1))) & 15
                code = (fb[y * STRIDE + x // 4] >> (6 - 2 * (x % 4))) & 3
                word = clut[2 * idx + (code >> 1)]
                pix = word & 0xFFFF if code & 1 else word >> 16
                want = (ext4(pix >> 8 & 15), ext4(pix >> 4 & 15), ext4(pix & 15))
            else:
                want = (0, 0, 0)
            got = img.getpixel((x * SCALE, y * SCALE))
            if got != want:
                if bad < 10:
                    print(f"x {x} y {y}: {got}, want {want}")
                bad += 1
    print(f"check_pattern.py: {W}x{PANEL_H} pixels, {bad} differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
