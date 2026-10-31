#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# gb24st.sh: build u8g2_font_gb24st_t_solder.c, the 24x24 font of the
# soldering station demo, from the X11 bitmap font gb24st (ISAS Song ti,
# GB2312, 24x24 dots; Debian package xfonts-base).
#
#   1. fontforge: gb24st.pcf to BDF. The glyphs keep their GB2312 codes.
#   2. python: GB2312 codes to Unicode. The full-width Latin letters, digits
#      and signs (U+FF01..U+FF5E) and the ideographic space (U+3000) move to
#      ASCII 0x21..0x7E and 0x20: "BC1" in a C string draws the full-width
#      24x24 glyphs, 3x3 tiles each, on the 8-pixel grid of clut_dma.
#   3. bdfconv (u8g2 tools/font/bdfconv): only the characters in solder.txt.
#
# Every glyph has a 24x24 cell: advance 24, ascent 22, descent 2. The ink
# stays inside the cell. Drawn at x = 8*i and baseline y = 8*j + 22, a
# character covers 3x3 whole tiles and can have its own colors. Not with
# u8g2_SetFontPosTop(): u8g2 takes the ascent of 'A', 21, one row short.
#
# usage: ./gb24st.sh     GB24ST, BDFCONV, FONTFORGE: paths of the tools

set -eu
cd "$(dirname "$0")"
GB24ST=${GB24ST:-/usr/share/fonts/X11/misc/gb24st.pcf.gz}
BDFCONV=${BDFCONV:-bdfconv}
FONTFORGE=${FONTFORGE:-fontforge}
OUT=u8g2_font_gb24st_t_solder.c

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

zcat -f "$GB24ST" > "$tmp/gb24st.pcf"
"$FONTFORGE" -lang=ff -c 'Open($1); Generate($2, "bdf")' \
  "$tmp/gb24st.pcf" "$tmp/gb24st." > "$tmp/fontforge.log" 2>&1
python3 -I - "$tmp/gb24st-24.bdf" "$tmp/gb24st-a.bdf" <<'EOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
out = []
for line in open(src, encoding='latin-1'):
    if line.startswith('ENCODING '):
        e = int(line.split()[1])
        try:
            u = ord(bytes([(e >> 8) | 0x80, (e & 0xff) | 0x80]).decode('gb2312'))
        except (UnicodeDecodeError, ValueError):
            u = -1                          # not a GB2312 character
        if 0xFF01 <= u <= 0xFF5E:
            u -= 0xFEE0                     # full-width to ASCII
        elif u == 0x3000:
            u = 0x20                        # ideographic space
        line = 'ENCODING %d\n' % u
    out.append(line)
open(dst, 'w', encoding='latin-1').writelines(out)
EOF
# the map: the code of every character in solder.txt, and the space. not
# bdfconv -u, which also takes all of ASCII
map=$(python3 -I -c 'import sys
s = set(open(sys.argv[1], encoding="utf-8").read()) - set("\n\t") | {" "}
print(",".join(str(ord(c)) for c in sorted(s)))' solder.txt)
"$BDFCONV" -f 1 -b 0 -m "$map" -n u8g2_font_gb24st_t_solder \
  -o "$tmp/font.c" "$tmp/gb24st-a.bdf" > "$tmp/bdfconv.log"

{
  cat <<'EOF'
/*
  u8g2_font_gb24st_t_solder: the characters of solder.txt, from the X11
  bitmap font gb24st (ISAS Song ti, GB2312, 24x24). Full-width Latin
  letters, digits and signs at their ASCII codes. Made by gb24st.sh.

  Copyright (C) 1988  The Institute of Software, Academia Sinica.

  Correspondence Address:  P.O.Box 8718, Beijing, China 100080.

  Permission to use, copy, modify, and distribute this software and its
  documentation for any purpose and without fee is hereby granted, provided
  that the above copyright notices appear in all copies and that both those
  copyright notices and this permission notice appear in supporting
  documentation, and that the name of "the Institute of Software, Academia
  Sinica" not be used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.  The Institute
  of Software, Academia Sinica, makes no representations about the suitability
  of this software for any purpose.  It is provided "as is" without express or
  implied warranty.

  THE INSTITUTE OF SOFTWARE, ACADEMIA SINICA, DISCLAIMS ALL WARRANTIES WITH
  REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
  AND FITNESS, IN NO EVENT SHALL THE INSTITUTE OF SOFTWARE, ACADEMIA SINICA,
  BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
  OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "u8g2.h"
EOF
  cat "$tmp/font.c"
} > "$OUT"
echo "$OUT: $(grep -o '\[[0-9]*\]' "$OUT" | head -1) bytes"
