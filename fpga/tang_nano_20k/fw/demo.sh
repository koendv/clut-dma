#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# demo.sh: the u8g2 examples on the display, one after the other, then a walk
# through the menu of demo_mui, with keys sent over the uart, one per second.
# The board must have the bitstream loaded (make load in ..). The firmware is
# built first, then the script waits for enter, to start a video recording.
# It prints the time of each step, for cutting and captions.
#
# usage: ./demo.sh     TTY: the uart of the board, default /dev/ttyUSB1

set -eu
cd "$(dirname "$0")"
TTY=${TTY:-/dev/ttyUSB1}

t0=0
step() {
  echo "$(( $(date +%s) - t0 )) s: $1"
}

# load an example, and show it for some seconds
show() {
  step "$1"
  make -s "$1" > /dev/null
  sleep "$2"
}

make -s palette.elf clutcolor.elf clutclock.elf graphics.elf mui.elf
printf 'start the recording, then press enter: '
read _
t0=$(date +%s)

show palette 5
show clutcolor 10
show clutclock 10
show graphics 20

# demo_mui: d next value, s next field, enter select (main.c, testimg.c)
step mui
make -s mui > /dev/null
stty -F "$TTY" 115200 raw -echo -hupcl
exec 3>"$TTY"
sleep 2                         # display init and the first menu page
for keys in E ddddd sddddd sddddd s E s E; do
  for k in $(echo "$keys" | fold -w1); do
    if [ "$k" = E ]; then
      printf '\r' >&3
    else
      printf '%s' "$k" >&3
    fi
    sleep 1
  done
done
exec 3>&-
step end
