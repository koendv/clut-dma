#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# mui2bpp_demo.sh: the anti-aliased menu (mui2bpp.c), with keys sent over
# the uart, for a video. The same tour in English and in Chinese: the
# values form (brightness, other status icons, baud rate, stop bits; up in
# English, down again in Chinese), then the about form with the numbers of
# the build and the measured times; between them the switch of language.
# The end on the Chinese about form. About 50 s. The same keys on both
# builds.
# Runs in the fw directory of a build, the Cortex-M1 or the GoRV32 one:
# make demo-menu there. The board must have the bitstream loaded (make load
# in .., make bit in the GoRV32 fw). The firmware is built first, then the
# script waits for enter, to start a video recording. It prints the time of
# each step, for cutting and captions.
#
# usage: make demo-menu     TTY: the uart of the board, default /dev/ttyUSB1
#        OPTS: build options of make mui2bpp, default STATS=1 (the measured
#        times on the about form); e.g. OPTS="STATS=1 USE_CLUT_DMA=0"

set -eu
TTY=${TTY:-/dev/ttyUSB1}
OPTS=${OPTS-STATS=1}

t0=0
step() {
  echo "$(( $(date +%s) - t0 )) s: $1"
}

# keys: s next field, w previous field, E select, i other status icons.
# pause: seconds after each key
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

make -s -B mui2bpp.elf $OPTS
printf 'start the recording, then press enter: '
read _
t0=$(date +%s)

step "English: the main menu"
make -s mui2bpp $OPTS > /dev/null 2>&1
stty -F "$TTY" 115200 raw -echo -hupcl
exec 3>"$TTY"
sleep 1

step "values: brightness up, other status icons, baud rate up, stop bits 2"
keys s 1
keys E 1.5
keys E 1
keys ssss 0.3
keys E 1
keys i 0.1
keys sE 1
keys ss 0.3
keys E 1
keys ss 0.3
keys E 1

step "back, about: the numbers of the build, the measured times"
keys s 1
keys E 1.5
keys i 0.1
keys s 1
keys E 2.5
keys i 2.5

step "the main menu button, settings: the language Chinese"
keys E 1.5
keys ww 0.3                      # the main menu keeps the cursor on about
keys E 1.5
keys Es 1
keys E 2

step "Chinese, values: brightness down, other status icons, baud rate down, stop bits 1"
keys s 1
keys E 1.5
keys E 1
keys wwww 0.3
keys E 1
keys i 0.1
keys sE 1
keys ww 0.3
keys E 1
keys sE 1

step "back, about"
keys ss 0.3
keys E 1.5
keys i 0.1
keys s 1
keys E 3
keys i 3

exec 3>&-
step end
