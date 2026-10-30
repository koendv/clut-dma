# clut-dma

Color for a 1 bpp frame buffer, in hardware.

https://github.com/user-attachments/assets/efce7386-0580-4d9f-b6e8-e7e7da05f02c

The video shows a Cortex-M1 soft core on a Tang Nano 20K FPGA, running the
u8g2 graphics library on an ST7789 display. Each demo uses a full-screen frame
buffer and fits in about 10 kB of RAM; a 16-bit color frame buffer for the
same 240×280 display would take 134 kB. The CPU draws in black and white; the
DMA engine adds the color while it sends the image to the display.

For the 240×280 display the bitmap, tile map and CLUT take 8,989 bytes; with
a page buffer of one tile row (8 pixel rows), 829 bytes. Because the engine,
not the CPU, does the color expansion, the CPU is free while the image is
sent. With two bitmaps, the CPU draws the next frame while the engine sends
the current one. With two page buffers of one tile row, the CPU draws a tile
row while the engine sends the previous one, in 1,069 bytes.

## Why

I made a debugger with an ST7789 color display and LVGL
([at32f405-app](https://github.com/koendv/at32f405-app)). The graphics took
the RAM, and the LVGL tick took the real-time behavior of the CPU. The
second version
([arm_can_tool](https://github.com/koendv/arm_can_tool)) has an SH1107
OLED and u8g2. That works well, but the OLED is single-source.

I want a display that is low-cost, easily sourced, uses little RAM, and
does not use the CPU when the image does not change.

The color drivers of the u8g2 branch `clut` let u8g2 drive the color
displays that every supplier sells. clut_dma does the same in hardware: a
low-cost MCU can drive a color display, and a larger MCU keeps its RAM and
its CPU time for the application, not for the graphics.

## Description

Three areas in memory:

- a bitmap of 1 bit per pixel, in which the CPU draws;
- a tile map, with a 4-bit index for each 8×8 tile;
- a color lookup table (CLUT) of 16 foreground/background color pairs.

The DMA engine reads them and sends RGB565, RGB444 or RGB666 pixels straight
to an SPI display: no color frame buffer.

The bitmap is in row layout (a byte
is 8 pixels of a pixel row, leftmost pixel in bit 7), as the 1-bit buffers of
u8g2, LVGL and Adafruit GFX.

A request is a rectangle; the CPU writes
FB_START, FIRST, LINE_OFFSET (bitmap bytes of a pixel row outside the
rectangle) and SIZE.

With the Verilog parameter CACHE = 0 the engine reads
the map, bitmap and CLUT words for every tile. With CACHE = 1 (default) the
engine reads them only when the next tile needs a different map, bitmap or
CLUT word.

## Software

The same color expansion in software, without the DMA engine:

- [u8g2, branch clut](https://github.com/koendv/u8g2/tree/clut): u8g2 with
  color drivers for ST7789, ILI9341 and GC9A01 displays, for Arduino.
  `u8x8_clut_stream_rgb565()` and `u8x8_clut_stream_rgb444()` in
  `csrc/u8x8_clut.c` are the software version of the engine.
- [micropython-u8g2](https://github.com/koendv/micropython-u8g2): u8g2
  graphics and mui menus for MicroPython, with the same color drivers.

## Contents

- `rtl/clut_dma.v`: Verilog model of the DMA engine, APB3 register port. Its
  `ifdef FORMAL` block holds the formal properties.
- `tb/`: testbench. `ref.c` writes the memory and the requests, and the
  bytes u8g2 `u8x8_clut_stream_rgb565()` and `u8x8_clut_stream_rgb444()`
  send for them; `tb_clut_dma.v` compares the model with those bytes, and
  checks the number of memory reads and the registers at the end. `ge.awk`
  turns a yosys gate count into gate equivalents, for `make size`.
- `formal/`: SymbiYosys. APB3 slave properties (`fapb3_slave.v`) for clut_dma
  and spi_lcd, the invariants of clut_dma, AHB-Lite slave properties
  (`fahbl_slave.v`) and a check of the contents of fbmem.
- `fpga/rtl/`: the rest of the FPGA prototype: `clut_fabric.v` (clut_dma,
  fbmem and spi_lcd on one APB and one AHB slot), `fbmem.v` (dual port
  memory, AHB-Lite for the CPU, a read port for clut_dma), `spi_lcd.v` (SPI
  master for the display, with backlight PWM).
- `fpga/tb/`: `tb_fabric.v`, testbench of clut_fabric as the CPU uses it;
  `tb_frame.v` and `st7789.py`, the simulated display image of a firmware
  test.
- `fpga/tang_nano_20k/`: the prototype on a Tang Nano 20K with a Gowin
  Cortex-M1 soft core and a WeAct ST7789 240×280 display: top level, pin
  constraints, Gowin project and build script, OpenOCD configuration
  (`m1.cfg`). `size.sh`: the size of clut_dma in the FPGA.
- `fpga/size/`: stubs of clut_dma and clut_fabric, for `size.sh`.
- `fpga/tang_nano_20k/fw/`: firmware for the Cortex-M1. Test images, and the
  color examples of the u8g2 branch `clut` (ClutPalette, ClutColor, ClutClock,
  demo_graphics, demo_mui) through clut_dma, with a u8x8 display driver that
  sends with clut_dma (`testimg.c`). `host.c` builds the same test on the PC,
  for the simulation.
- `nightly.sh`: the overnight tests, on a snapshot of the last commit.
- `clut-dma.md`, `clut-dma.pdf`: the publication. `fig/`: its two figures, SVG.

## Requirements

- OSS CAD Suite (tested 2026-10-28: Yosys 0.70, SymbiYosys with yices,
  Icarus Verilog 14, Verilator 5.053), on the PATH.
- u8g2 with the color drivers, branch
  [clut](https://github.com/koendv/u8g2/tree/clut) of koendv/u8g2, in
  `../u8g2` (or `make U8G2=...`).
- A C compiler, for `tb/ref.c` and `fw/host.c`.
- PDF: pandoc, xelatex, Noto Serif CJK SC, rsvg-convert (for the figures).
- FPGA prototype only:
  - Gowin EDA (tested V1.9.12.02 SP2, `/opt/Gowin_V1.9.12.02_SP2`, or
    `make GWPATH=...`) for the bitstream. The Cortex-M1 is Gowin IP and is
    not included: generate `src/gowin_empu_m1/gowin_empu_m1.v` from
    `gowin_empu_m1.ipc` with the IP Core Generator of the Gowin IDE.
  - openFPGALoader, to load the bitstream.
  - arm-none-eabi-gcc (tested xPack 14.2.1) and OpenOCD (tested xPack
    0.12.0) with a CMSIS-DAP probe, for the firmware. Paths in
    `fw/Makefile`: `CROSS`, `OPENOCD`.
  - Python 3 with Pillow, for `make sim`.

## Use

In the top directory:

```
make test          # simulate, compare with u8g2; also the read counts and registers
make test-nocache  # the same with CACHE = 0
make fabric        # clut_fabric as the cpu uses it, with the display window commands
make lint          # verilator lint of all rtl
make size          # yosys flip-flops, gates and gate equivalents, CACHE = 1 and 0
make formal        # SymbiYosys, the short tasks, about 8 minutes
make measure       # spi underrun for spi clock and memory wait states, about 15 minutes;
                   # make measure WAIT_STATES="16 24 32" for external memory
make pdf           # clut-dma.pdf, README.pdf
make zip           # build/clut-dma.zip, the files of the last commit
./nightly.sh &     # the long tests, results in build/nightly/latest/summary.txt
```

In `fpga/tang_nano_20k/`:

```
make               # bitstream, impl/pnr/clut_m1.fs
make load          # into the sram of the fpga
make flash         # into the flash of the board
./size.sh          # LUTs, ALUs, flip-flops and block RAMs of clut_dma: the
                   # processor alone, without clut_dma, and the whole design
```

In `fpga/tang_nano_20k/fw/`:

```
make load          # test: cpu fills, then clut_dma test pattern and rectangles
make border        # test: a colored line on each border
make palette       # u8g2 ClutPalette
make clutcolor     # u8g2 ClutColor, a dashboard updated every second
make clutclock     # u8g2 ClutClock, an analog clock
make graphics      # u8g2 demo_graphics, the GraphicsTest pages
make mui           # u8g2 demo_mui, a menu; keys over the uart
make demo          # the examples one after the other, then a walk through the
                   # menu with keys over the uart; waits for enter, for a video
make term          # serial terminal on UART0, /dev/ttyUSB1, 115200 baud
make sim TEST=...  # the image the display ought to show, from a simulation of
                   # the rtl: border, palette, clutcolor, clutclock, graphics, mui
```

Build options:

- `LCD_DIV`: SPI clock, 54 MHz / (2 * LCD_DIV), default 1.
- `LCD_BRIGHT`: backlight, n/256, default 128.
- `LCD_MADCTL`: 0x00: connector at the top, 0xC0: at the bottom.
- `PAGE_ROWS`: u8g2 page buffer of 1 or 2 tile rows for clutclock and
  graphics, default the full screen.

Change an option with `make -B` or after `make clean`.

## FPGA prototype

Tang Nano 20K, Gowin Cortex-M1 at 54 MHz with 32 KiB ITCM and 32 KiB DTCM,
clut_fabric on its APB (0x60000000) and AHB (0x80000000, 16 KiB fbmem).
UART0 (0x50004000) goes to the USB serial of the board. The firmware is loaded
into ITCM over SWD and started with a reset of the core (`openocd`, `m1.cfg`).

| signal | FPGA pin | header | display |
|---|---|---|---|
| lcd_sck | 75 | J5 | SCL |
| lcd_mosi | 72 | J5 | SDA |
| lcd_rst_n | 49 | J5 | RES |
| lcd_dc | 71 | J5 | DC |
| lcd_cs_n | 86 | J5 | CS |
| lcd_bl | 76 | J6 | BLK |
| swclk | 73 | J6 | SWD probe |
| swdio | 74 | J6 | SWD probe |
| mark | 16 | J6 | timing mark, high during a transfer |

The display is powered from 3V3 and GND.

Screen update of ClutClock, measured on the board: the whole clock drawn by
u8g2 and the whole screen sent, 240×280 in RGB444, SPI at 27 MHz.

| u8g2 buffer | RAM | draw and send |
|---|---|---|
| 1 tile row (`PAGE_ROWS=1`) | 1,605 bytes | 135 ms |
| 2 tile rows (`PAGE_ROWS=2`) | 1,845 bytes | 89 ms |
| full screen | 9,765 bytes | 42 ms |

RAM: the bitmap (240, 480 or 8,400 bytes), the tile map (525 bytes) and the
CLUT (64 bytes), plus 776 bytes of variables and stack (the highest stack
use measured). The code is in ITCM and not counted. Of each time, 29.9 ms is
the send by clut_dma: 100,800 bytes, the SPI never waits. The rest is
drawing; in page mode u8g2 draws the clock once per page. The current
firmware waits for each transfer to end, so drawing and sending do not
overlap.

## Defensive publication

[clut-dma.md](clut-dma.md) ([PDF](clut-dma.pdf)): *Color expansion of a 1-bpp
frame buffer with a per-tile color lookup table in an MCU DMA engine.* It
discloses the method and its variants so that they are prior art.

## License

Text (`clut-dma.md`, `clut-dma.pdf`): CC BY 4.0. Code (Verilog model,
testbenches, formal properties, firmware, scripts): BSD-2-Clause, see
`LICENSE`. The Gowin Cortex-M1 soft core is IP of the FPGA vendor and is not
included; only its configuration file `gowin_empu_m1.ipc` is.
