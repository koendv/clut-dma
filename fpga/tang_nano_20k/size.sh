#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# size.sh: the size of clut_dma in the fpga, as in clut-dma.md section 4.4.
# Three builds with Gowin EDA, in build/size:
#   cpu    the processor alone: clut_fabric replaced by a stub
#   nodma  the processor and clut_fabric, clut_dma replaced by a stub
#   full   the whole design
# Prints LUTs, ALUs, flip-flops and block RAMs of each, and the differences.
# About 2 minutes. Needs src/gowin_empu_m1/gowin_empu_m1.v, generated from
# gowin_empu_m1.ipc with the Gowin IP Core Generator.
#
# usage: ./size.sh     GWPATH: the Gowin EDA install, as in the Makefile

set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out="$repo/build/size"
GWPATH=${GWPATH:-/opt/Gowin_V1.9.12.02_SP2}

# a copy of the design, with a stub in place for cpu and nodma
build() {
  d="$out/$1"
  rm -rf "$d"
  mkdir -p "$d/fpga/tang_nano_20k" "$d/rtl"
  cp -r "$repo/fpga/rtl" "$d/fpga/"
  cp "$repo/rtl/clut_dma.v" "$d/rtl/"
  cp -r "$here/build.tcl" "$here/top.v" "$here/tang_nano_20k.cst" \
        "$here/tang_nano_20k.sdc" "$here/Makefile" "$here/src" "$d/fpga/tang_nano_20k/"
  case "$1" in
    cpu)   cp "$repo/fpga/size/clut_fabric_stub.v" "$d/fpga/rtl/clut_fabric.v" ;;
    nodma) cp "$repo/fpga/size/clut_dma_stub.v" "$d/rtl/clut_dma.v" ;;
  esac
  echo "build $1"
  make -C "$d/fpga/tang_nano_20k" GWPATH="$GWPATH" > "$d/make.log" 2>&1
}

# luts alus flip-flops bsram, from the report of a build
usage() {
  r="$out/$1/fpga/tang_nano_20k/impl/pnr/clut_m1.rpt.txt"
  sed -n 's/.*LUT,ALU,ROM16 *| *[0-9]*(\([0-9]*\) LUT, \([0-9]*\) ALU.*/\1 \2/p' "$r" | tr '\n' ' '
  sed -n 's/.*Logic Register as FF *| *\([0-9]*\)\/.*/\1/p' "$r" | tr '\n' ' '
  sed -n 's/^ *BSRAM *| *\([0-9]*\)\/.*/\1/p' "$r"
}

for b in cpu nodma full; do
  build $b
done

{
  echo "cpu $(usage cpu)"
  echo "nodma $(usage nodma)"
  echo "full $(usage full)"
} | awk '
  { name[NR] = $1; lut[NR] = $2; alu[NR] = $3; ff[NR] = $4; bs[NR] = $5 }
  END {
    printf "%-28s %6s %6s %6s %6s\n", "", "LUT", "ALU", "FF", "BSRAM"
    printf "%-28s %6d %6d %6d %6d\n", "processor alone", lut[1], alu[1], ff[1], bs[1]
    printf "%-28s %6d %6d %6d %6d\n", "processor + fabric, no dma", lut[2], alu[2], ff[2], bs[2]
    printf "%-28s %6d %6d %6d %6d\n", "whole design", lut[3], alu[3], ff[3], bs[3]
    printf "%-28s %6d %6d %6d %6d\n", "clut_dma", lut[3] - lut[2], alu[3] - alu[2], ff[3] - ff[2], bs[3] - bs[2]
    printf "%-28s %6d %6d %6d %6d\n", "spi_lcd, fbmem and glue", lut[2] - lut[1], alu[2] - alu[1], ff[2] - ff[1], bs[2] - bs[1]
    printf "clut_dma: %.0f %% of the logic (LUT + ALU) and %.0f %% of the flip-flops of the processor\n",
      100 * (lut[3] - lut[2] + alu[3] - alu[2]) / (lut[1] + alu[1]), 100 * (ff[3] - ff[2]) / ff[1]
  }'
