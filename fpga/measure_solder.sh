#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# measure_solder.sh: the soldering station demo (make solder) in every
# variant, measured on the board, without a person at the keyboard.
# Per variant: build, load, a visit to the menu (for the highest stack
# use), key b (50 frames back to back), and the lines the firmware prints.
# The bitstream of the build is loaded first.
#
# usage: ./measure_solder.sh arm|riscv     TTY: the uart, default /dev/ttyUSB1
#                                          U8G2: as in fw/Makefile
# No terminal may read the uart at the same time.

set -u
case "${1:-}" in
  arm)   dir=tang_nano_20k_arm ;;
  riscv) dir=tang_nano_20k_riscv ;;
  *)     echo "usage: $0 arm|riscv" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/$dir" || exit 1
TTY=${TTY:-/dev/ttyUSB1}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mk() {
  if [ -n "${U8G2:-}" ]; then make U8G2="$U8G2" "$@"; else make "$@"; fi
}

make -s load > "$tmp/bit.log" 2>&1 || { echo "bitstream: load failed"; exit 1; }
cd fw
stty -F "$TTY" 115200 raw -echo -echoe -echok

while read -r v; do
  [ "$v" = default ] && v=""
  echo "== ${v:-default}"
  if ! mk -s -B solder.elf $v > "$tmp/build.log" 2>&1; then
    if grep -q 'left for the stack' "$tmp/build.log"; then
      echo "does not fit: $(grep -o 'On-Chip RAM: .*stack' "$tmp/build.log" | head -1)"
    else
      echo "build failed: $(grep -m1 -i error "$tmp/build.log")"
    fi
    continue
  fi
  timeout 35 cat "$TTY" > "$tmp/uart.txt" &
  cat_pid=$!
  sleep 0.3
  mk -s solder $v > /dev/null 2>&1
  sleep 4
  # the menu: open, a value up, two fields down to 返回, back
  for k in ' ' d s s; do printf '%s' "$k" > "$TTY"; sleep 0.5; done
  printf '\r' > "$TTY"
  sleep 1
  printf 'b' > "$TTY"
  wait $cat_pid
  tr -d '\r' < "$tmp/uart.txt" > "$tmp/out.txt"
  grep -a 'ram:' "$tmp/out.txt" | tail -1
  grep -a '^benchmark: [0-9]' "$tmp/out.txt" | tail -1
  grep -a '^frames per second' "$tmp/out.txt" | tail -1
done <<EOF
USE_CLUT_DMA=0
USE_CLUT_DMA=0 COLOR=1
PAGE_ROWS=1
PAGE_ROWS=1 COLOR=1
default
COLOR=1
PAGE_ROWS=35
PAGE_ROWS=35 COLOR=1
EOF
