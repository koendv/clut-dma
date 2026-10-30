#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
"""
st7789.py: a model of the ST7789 display, for the spi bytes of tb_frame.v.
Draws the bytes in a 240x320 display memory and writes the 240x280 window
the panel shows, as seen with the connector of the WeAct module at the top.

Commands: CASET 0x2A, RASET 0x2B, RAMWR 0x2C, MADCTL 0x36 (MX, MY; not MV),
COLMOD 0x3A (rgb565, rgb444, rgb666). Others are ignored, among them
display inversion: an IPS panel with INVON shows the colors as sent.

usage: st7789.py spi.txt outdir [names.txt]
writes outdir/frame_NN.png after each RAMWR, or at each "f" line (the end of
a frame of several requests) if spi.txt has them, and outdir/final.png; 2x
size.
"""

import os
import sys

from PIL import Image

RAM_W, RAM_H = 240, 320
YOFS, VIS_H = 20, 280
SCALE = 2


def main():
    spi, outdir = sys.argv[1], sys.argv[2]
    names = []
    if len(sys.argv) > 3 and os.path.exists(sys.argv[3]):
        names = [line.strip() for line in open(sys.argv[3])]

    ram = [[(0, 0, 0)] * RAM_W for _ in range(RAM_H)]
    xs, xe, ys, ye = 0, RAM_W - 1, 0, RAM_H - 1
    madctl, colmod = 0, 0x66
    cmd, params, pix = None, [], []
    col = row = 0
    frames = []
    written = False

    def snapshot():
        img = Image.new("RGB", (RAM_W, VIS_H))
        for y in range(VIS_H):
            for x in range(RAM_W):
                img.putpixel((x, y), ram[YOFS + y][x])
        return img.resize((RAM_W * SCALE, VIS_H * SCALE), Image.NEAREST)

    def put(rgb):
        nonlocal col, row
        px = RAM_W - 1 - col if madctl & 0x40 else col     # MX
        py = RAM_H - 1 - row if madctl & 0x80 else row     # MY
        if 0 <= px < RAM_W and 0 <= py < RAM_H:
            ram[py][px] = rgb
        col += 1
        if col > xe:
            col = xs
            row += 1
            if row > ye:
                row = ys

    def ext(v, bits):
        # n bits to 8 bits, msb replicated
        return (v << (8 - bits)) | (v >> (2 * bits - 8)) if bits < 8 else v

    lines = open(spi).read().split("\n")
    marked = "f" in lines

    for line in lines:
        if not line:
            continue
        if line == "f":
            frames.append(snapshot())
            written = False
            continue
        kind, val = line.split()
        b = int(val, 16)
        if kind == "c":
            if written and not marked:
                frames.append(snapshot())
                written = False
            cmd, params, pix = b, [], []
            if cmd == 0x2C:
                col, row = xs, ys
            continue
        if cmd == 0x2C:
            pix.append(b)
            fmt = colmod & 7
            if fmt == 5 and len(pix) == 2:
                v = pix[0] << 8 | pix[1]
                put((ext(v >> 11, 5), ext(v >> 5 & 63, 6), ext(v & 31, 5)))
                pix = []
            elif fmt == 3 and len(pix) == 3:
                v = pix[0] << 16 | pix[1] << 8 | pix[2]
                for p in (v >> 12, v & 0xFFF):
                    put((ext(p >> 8, 4), ext(p >> 4 & 15, 4), ext(p & 15, 4)))
                pix = []
            elif fmt == 6 and len(pix) == 3:
                put(tuple(ext(c >> 2, 6) for c in pix))
                pix = []
            written = True
            continue
        params.append(b)
        if cmd == 0x2A and len(params) == 4:
            xs, xe = params[0] << 8 | params[1], params[2] << 8 | params[3]
        elif cmd == 0x2B and len(params) == 4:
            ys, ye = params[0] << 8 | params[1], params[2] << 8 | params[3]
        elif cmd == 0x36:
            madctl = b
            if madctl & 0x20:
                sys.exit("st7789.py: MADCTL MV is not modelled")
        elif cmd == 0x3A:
            colmod = b
    if written and not marked:
        frames.append(snapshot())

    os.makedirs(outdir, exist_ok=True)
    for i, img in enumerate(frames):
        img.save(os.path.join(outdir, "frame_%02d.png" % i))
        print("frame_%02d.png %s" % (i, names[i] if i < len(names) else ""))
    if frames:
        frames[-1].save(os.path.join(outdir, "final.png"))


if __name__ == "__main__":
    main()
