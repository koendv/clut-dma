#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# solder_demo.sh: the soldering station demo (solder.c), with keys sent
# over the uart: heating up from cold to 320, the set temperature up to 350
# with w, down to 300 in the menu. For a video. The same keys on both builds.
# Runs in the fw directory of a build, the Cortex-M1 or the GoRV32 one:
# make demo-solder there. The board must have the bitstream loaded (make
# load in ..). The firmware is built first, then the script waits for enter,
# to start a video recording. It prints the time of each step, for cutting
# and captions.
#
# usage: make demo-solder     TTY: the uart of the board, default /dev/ttyUSB1
#        OPTS: build options of make solder, default COLOR=1; empty (OPTS=)
#        for black and white. e.g. OPTS="COLOR=1 PAGE_ROWS=1", OPTS="COLOR=1 USE_CLUT_DMA=0"

set -eu
TTY=${TTY:-/dev/ttyUSB1}
OPTS=${OPTS-COLOR=1}

t0=0
step() {
  echo "$(( $(date +%s) - t0 )) s: $1"
}

# keys: w s set temperature, E enter; in the menu w s field, a d value,
# E select. pause: seconds after each key
keys() {
  for k in $(echo "$1" | fold -w1); do
    if [ "$k" = E ]; then
      printf '\r' >&3
    else
      printf '%s' "$k" >&3
    fi
    sleep "$2"
  done
}

make -s -B solder.elf $OPTS
printf 'start the recording, then press enter: '
read _
t0=$(date +%s)

step "solder ${OPTS:-black and white}: cold start, heating to 320"
make -s solder $OPTS > /dev/null 2>&1
stty -F "$TTY" 115200 raw -echo -hupcl
exec 3>"$TTY"
# the waits: the simulated iron within a degree of the set temperature
# (7.7 s from cold, 2.0 s up 30, 2.8 s down 50), then the shown power at
# rest (an average over 8 steps of 0.1 s)
sleep 12

step "set temperature up to 350"
keys wwwwww 0.4
sleep 6

step "menu: set temperature down to 300"
keys E 1
keys aaaaaaaaaa 0.4
keys ss 1
step "back: cooling to 300"
keys E 9

exec 3>&-
step end
