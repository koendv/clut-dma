#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
"""
clutline.py: the four colors of a clut line at 2 bits per pixel, from a
foreground and a background, as u8g2_SetRoleColor() of u8g2.h computes
them: code 0 the background, 3 the foreground, 1 and 2 the blends at a
third and two thirds, mixed in linear light (c^gamma per channel, mixed,
back with the power 1/gamma), rounded to the native format. gamma: 2.2 for
light text on a darker background, 1.0 (-d) for dark text on a lighter
background, the lighter by luminance, as U8G2_GAMMA and U8G2_GAMMA_DARK.

For a compiler that does not accept u8g2_SetRoleColor() (it needs the math
builtins of GCC in a static initializer): the output is the arguments of
u8g2_SetRoleClut(u8g2, role, c0, c1, c2, c3).

usage: clutline.py [-f 444|565] [-g gamma] [-d gamma] FG BG     (rgb888, hex)
  clutline.py E8E8E8 101828
  0x112, 0x888, 0xbbb, 0xeee   /* E8E8E8 on 101828, gamma 2.2, rgb444 */
"""

import argparse


def native(f, b, k, fmt, gamma):
    """k thirds of the way from b to f, in the native format"""
    bits = (5, 6, 5) if fmt == 565 else (4, 4, 4)
    v = 0
    for s, n in zip((16, 8, 0), bits):
        fc, bc = ((f >> s) & 0xFF) / 255, ((b >> s) & 0xFF) / 255
        x = ((fc ** gamma * k + bc ** gamma * (3 - k)) / 3) ** (1 / gamma)
        v = v << n | int(x * ((1 << n) - 1) + 0.5)
    return v


def main():
    p = argparse.ArgumentParser(description="the four colors of a clut line, 2 bits per pixel")
    p.add_argument("-f", type=int, choices=(444, 565), default=444, help="native format, default 444")
    p.add_argument("-g", type=float, default=2.2, help="gamma, light on dark, default 2.2")
    p.add_argument("-d", type=float, default=1.0, help="gamma, dark on light, default 1.0")
    p.add_argument("fg", help="foreground, rgb888 hex")
    p.add_argument("bg", help="background, rgb888 hex")
    a = p.parse_args()
    f, b = int(a.fg, 16), int(a.bg, 16)
    y = lambda v: 2126 * ((v >> 16) & 0xFF) + 7152 * ((v >> 8) & 0xFF) + 722 * (v & 0xFF)
    gamma = a.g if y(f) >= y(b) else a.d
    c = [native(f, b, k, a.f, gamma) for k in range(4)]
    w = 3 if a.f == 444 else 4
    print(", ".join(f"0x{x:0{w}x}" for x in c),
          f"  /* {f:06X} on {b:06X}, gamma {gamma:g}, rgb{a.f} */")


if __name__ == "__main__":
    main()
