# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# ge.awk: gate equivalents of a yosys "stat" of generic gates (synth), as in
# clut-dma.md section 4.4: NAND2, NOR2 1; NOT 0.67; AND, OR, ANDNOT, ORNOT
# 1.33; XOR, XNOR, multiplexer 2.33; flip-flop 4.5, plus 1 with asynchronous
# reset, plus 1.5 with enable.
# usage: awk -f ge.awk stat.txt

$2 ~ /^\$_/ {
  n = $1; c = $2
  if (c ~ /DFF/) {
    w = 4.5
    if (c ~ /_P[NP][01]/) w += 1      # asynchronous reset or set
    if (c ~ /DFFE/) w += 1.5           # enable
    ff += n
  } else {
    if (c ~ /^\$_NOT_$/) w = 0.67
    else if (c ~ /^\$_(NAND|NOR)_$/) w = 1
    else if (c ~ /^\$_(AND|OR|ANDNOT|ORNOT)_$/) w = 1.33
    else w = 2.33                      # XOR, XNOR, MUX
    gates += n
  }
  ge += n * w
}

END { printf "%d flip-flops, %d gates, %.0f gate equivalents\n", ff, gates, ge }
