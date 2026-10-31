# clut_dma on a RISC-V

The RISC-V build of the prototype: [`rtl/clut_dma.v`](../../rtl/clut_dma.v), the same file as in the [ARM build](../tang_nano_20k_arm/README.md), with a Gowin GoRV32 soft core on a Tang Nano 20K, driving a WeAct ST7789 240×280 display.
For the engine itself, see [rtl/README.md](../../rtl/README.md).

Here clut_dma sits as it would in an MCU: its registers on the system bus, and its own bus master that reads the frame buffer from CPU RAM.

## Hardware

Gowin GoRV32 (based on VexRiscv, no caches) at 24 MHz, with 32 KiB On-Chip RAM for program and data.

| address | |
|---|---|
| 0x40000000 | On-Chip RAM, 32 KiB: program, data, and the frame buffer, tile map and CLUT |
| 0xE8000000 | AHB slave port: clut_dma registers, through `rtl/ahb_reg_slave.v` |
| 0xF8000040 | APB: spi_lcd, SPI master for the display, with backlight PWM |
| 0xF8000080 | APB: GPIO |
| 0xF0200000 | UART1, to the USB serial of the board |

clut_dma reads from On-Chip RAM through the AHB master port of GoRV32, with `rtl/ahb_rd_master.v`; the CPU and clut_dma share that RAM.
`rtl/clut_fabric_ahb.v` connects clut_dma and spi_lcd to the three ports.
The clut_dma interrupt is on EXT_INT[0].

The firmware is loaded into On-Chip RAM over JTAG and started at 0x40000000 (`openocd`, `rv.cfg`, standard RISC-V debug 0.13).

## Limits of this build

These are limits of the soft core on this FPGA, not of clut_dma:

- Clock: GoRV32 reaches about 35 MHz on the GW2AR-18, so 50 and 48 MHz do not close timing.
  The build runs at 24 MHz (27 MHz × 8 / 9) and the SPI at 12 MHz; a whole screen in RGB444 takes about twice as long as on the ARM build.
- RAM: the 64 KB On-Chip RAM configuration of GoRV32 is faulty: a byte store at times lands one word (4 bytes) further.
  The 32 KB configuration is correct, so program, data and frame buffer share 32 KiB.
- The firmware uses compressed instructions (rv32imc); with them, the graphics and mui demos fit in 32 KiB.

## Wiring

The display is wired as in the ARM build; the debug probe uses JTAG instead of SWD.

| signal | FPGA pin | header | display |
|---|---|---|---|
| GND | | J6 20 | GND |
| 3V3 | | J6 19 | VCC |
| lcd_sck | 18 | J6 18 | SCL |
| lcd_mosi | 19 | J6 17 | SDA |
| lcd_rst_n | 20 | J6 16 | RES |
| lcd_dc | 17 | J6 15 | DC |
| lcd_cs_n | 31 | J6 14 | CS |
| lcd_bl | 30 | J6 13 | BLK |
| jtag_tck | 73 | J6 1 | probe TCK |
| jtag_tms | 74 | J6 2 | probe TMS |
| jtag_tdi | 75 | J6 3 | probe TDI (TXD) |
| jtag_tdo | 76 | J5 3 | probe TDO (RXD) |
| mark | 16 | J6 7 | timing mark, high during a transfer |

TDO is on the other header, J5.
The probe takes its GND and VTref from J5 pin 15 (GND) and pin 16 (3V3).

## Requirements

Besides the [common requirements](../../rtl/README.md#requirements):

- Gowin EDA (tested V1.9.12.02 SP2, `/opt/Gowin_V1.9.12.02_SP2`, or
  `make GWPATH=...`) for the bitstream. GoRV32 is Gowin IP and is not
  included: in the Gowin IDE, open the project `tang_nano_20k_riscv.gprj`;
  in the IP Core Generator, load `src/gowin_gorv32/gowin_gorv32.ipc` and
  generate `src/gowin_gorv32/gowin_gorv32.v`. The bitstream is built with
  `make`, not in the IDE.
- openFPGALoader, to load the bitstream.
- riscv-none-elf-gcc (tested xPack 15.2.0) and OpenOCD (tested xPack
  0.12.0) with a CMSIS-DAP probe, for the firmware. Paths in
  `fw/Makefile`: `CROSS`, `OPENOCD`.

## Use

In this directory:

```
make               # bitstream, impl/pnr/clut_rv.fs
make load          # into the sram of the fpga
make flash         # into the flash of the board
```

In `fw/`:

```
make load          # test: cpu fills, then clut_dma test pattern and rectangles
make border        # test: a colored line on each border
make palette       # u8g2 ClutPalette
make clutcolor     # u8g2 ClutColor, a dashboard updated every second
make clutclock     # u8g2 ClutClock, an analog clock
make graphics      # u8g2 demo_graphics, the GraphicsTest pages
make mui           # u8g2 demo_mui, a menu; keys over the uart
make stars         # Adafruit GFX falling stars, 1 bpp, row layout
make solder        # soldering station, 24x24 Chinese, see below
make async         # the auto gauge of the ARM build, interrupt driven, see below
make demo-solder   # the same with keys over the uart, for a video, as in the ARM build
make term          # serial terminal on UART1, /dev/ttyUSB1, 115200 baud
```

The firmware is that of the ARM build: `testimg.c`, `stars.cpp` and the
Arduino stubs come from `../tang_nano_20k_arm/fw`. `main.c` is its own
copy, for the other platform: UART1, `mcycle` as the clock, the frame
buffer in On-Chip RAM, and a check of STATUS ERR, a bus error of the
clut_dma read master.

For per-character color, see the rules in the [ARM build](../tang_nano_20k_arm/README.md#per-character-color); they hold for both.

Build options, as in the ARM build: `LCD_DIV` (SPI clock, 24 MHz / (2 *
LCD_DIV), default 1), `LCD_BRIGHT`, `LCD_MADCTL`, `PAGE_ROWS`.

## Soldering station demo

The soldering station of the [ARM build](../tang_nano_20k_arm/README.md#soldering-station-demo), the same `solder.c` and `u8x8_clut_dma.c`, keys and build options (`PAGE_ROWS`, `COLOR`, `USE_CLUT_DMA`).
Here the buffers are in the On-Chip RAM of the CPU, and clut_dma reads them over AHB, as in an MCU.

Measured with `../../measure_solder.sh riscv`: GoRV32 at 24 MHz, SPI at 12 MHz, 240×280, key `b` (50 frames back to back).
RAM for the display: the bytes of the buffers, CLUT and map in On-Chip RAM.
CPU time for sending: as on the [ARM build](../tang_nano_20k_arm/README.md#soldering-station-demo), the time per frame the CPU spends sending the picture, everything but the drawing.

| variant | RAM for the display, b/w / color | per frame, b/w / color | CPU time for sending, color |
|---|---|---|---|
| software, 2 rows | 488 / 1,072 bytes | 236.1 / 244.8 ms | 140.6 ms, busy |
| 1 row | 248 / 832 bytes | 242.3 / 242.7 ms | 70.1 ms |
| 2 rows | 488 / 1,072 bytes | 170.2 / 170.7 ms | 70.1 ms |
| full screen | 8,408 / 8,992 bytes | 110.2 / 109.9 ms | 70.1 ms |

- On this slow CPU, software expands the colors slower than the SPI sends them: 141 ms of CPU time against 70 ms with clut_dma (67 ms of SPI, and the window commands of the 35 tile rows).
  With clut_dma a frame takes 171 instead of 245 ms.
- Color costs RAM, not time, as on the ARM build.
- The drawing time varies by a few ms with what is on the screen; the figures are from one run.
- The stack has the rest of the RAM; the linker keeps at least 1 KiB free (`rv.ld`). The highest stack measured: 504 bytes with clut_dma, 788 with `USE_CLUT_DMA=0`, both with the menu.

## Auto gauge demo

The auto gauge of the [ARM build](../tang_nano_20k_arm/README.md#auto-gauge-demo), the same `async.c`: the whole screen in one clut_dma request, the CPU asleep while it is sent.
14 frames per second: the send of a whole screen takes 67.2 ms at 12 MHz. CPU 3.5 to 6 %.

clut_dma is source 12 of the PLIC (`EXT_INT[0]`, GP0 of the GoRV32 SDK).
Interrupts stay off (`mstatus.MIE` 0): `wfi` wakes on the pending interrupt, and the wait loop claims it, clears clut_dma and completes it.
A trap handler, with `mstatus.MIE` on for one instruction between two `wfi`, did not work: the pending interrupt was not taken in that window, and the loop spun.
