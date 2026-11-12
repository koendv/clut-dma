# Getting started

There are three ways to start: without hardware, with the Arm build, or with the RISC-V build.
Each is a list of commands. The details are in the linked pages.

## The sources

```
git clone https://github.com/koendv/clut-dma
git clone -b clut https://github.com/koendv/u8g2
```

u8g2 is needed with the color drivers, branch `clut`, next to clut-dma.
The Makefiles look for it in `../u8g2`; another place is given with `make U8G2=...`.
mui_2bpp has its own copies and does not need it.

## A. Without hardware

The display is `fpga/tb/st7789.py`, a model of the ST7789 in Python. The bytes sent to the display are drawn in a PNG.

Needed: GCC, make, Python 3 with Pillow.
The colors of the menu are computed by GCC at compile time. With other compilers, the colors are computed by `mui_2bpp/tools/clutline.py` ([mui_2bpp](mui_2bpp/README.md#roles)).

The anti-aliased menu, on the PC:

```
cd clut-dma/mui_2bpp
make pattern       # the four codes of a pixel, checked against the spec
make demo          # the menu, a list of keys: build/demo/frame_NN.png
```

The RTL needs more: OSS CAD Suite on the PATH (Icarus Verilog, Yosys, Verilator), and u8g2.

```
cd clut-dma
make test          # clut_dma.v in simulation, compared with u8g2 byte for byte
cd fpga/tang_nano_20k_arm/fw
make sim TEST=clutclock
make sim-mui2bpp   # the menu through the rtl, about 2.5 minutes
```

After a change of the forms, the fonts are generated again with `make font` in mui_2bpp. This also needs fontTools.
All requirements: [rtl/README.md](rtl/README.md#requirements).

## B. The Arm build

Hardware:

- Tang Nano 20K;
- WeAct ST7789 display, 240×280, SPI;
- a CMSIS-DAP probe, for SWD;
- jumper wires.

Software:

- Gowin EDA, tested V1.9.12.02 SP2, in `/opt/Gowin_V1.9.12.02_SP2` (or `make GWPATH=...`);
- openFPGALoader;
- xPack arm-none-eabi-gcc 14.2.1 and xPack OpenOCD 0.12.0, in `/opt`; other places: `CROSS`, `OPENOCD` in `fw/Makefile`;
- minicom, for `make term`.

1. The Cortex-M1 is Gowin IP and is not in this repository. It is generated once, in the Gowin IDE: open `fpga/tang_nano_20k_arm/tang_nano_20k_arm.gprj`; in the IP Core Generator, load `src/gowin_empu_m1/gowin_empu_m1.ipc` and generate `src/gowin_empu_m1/gowin_empu_m1.v`. The bitstream is built with `make`, not in the IDE.

2. The bitstream:

   ```
   cd fpga/tang_nano_20k_arm
   make               # impl/pnr/clut_m1.fs
   make load          # into the sram of the fpga; gone at power off
   make flash         # or into the flash of the board
   ```

3. The wiring. The display is on header J6, pins 13 to 20. The probe: SWCLK on J6 pin 1, SWDIO on J6 pin 2, GND and VTref on J5 pins 15 and 16.
   The table: [wiring](fpga/tang_nano_20k_arm/README.md#wiring).

4. The firmware, in `fw/`:

   ```
   make load          # test: a test pattern and rectangles
   make mui2bpp       # the anti-aliased menu, English and Chinese
   make term          # the uart, /dev/ttyUSB1, 115200 baud: the keys of the menu
   ```

   The menu is controlled with `w` `s` or up and down, and space or enter.
   All demos: [Use](fpga/tang_nano_20k_arm/README.md#use).

## C. The RISC-V build

As the Arm build, with:

- the IP: `fpga/tang_nano_20k_riscv/tang_nano_20k_riscv.gprj`, `src/gowin_gorv32/gowin_gorv32.ipc`, generate `src/gowin_gorv32/gowin_gorv32.v`;
- xPack riscv-none-elf-gcc 15.2.0;
- the probe on JTAG: TCK on J6 pin 1, TMS on J6 pin 2, TDI on J6 pin 3, TDO on J5 pin 3. The table: [wiring](fpga/tang_nano_20k_riscv/README.md#wiring). With the JTAG wiring the Arm build works too: SWCLK is TCK, SWDIO is TMS.

```
cd fpga/tang_nano_20k_riscv
make               # impl/pnr/clut_rv.fs
cd fw
make bit           # the bitstream into the sram of the fpga
make mui2bpp
make term
```

The same demos run, slower: 24 MHz, SPI at 12 MHz.
All demos: [Use](fpga/tang_nano_20k_riscv/README.md#use).

## If it does not work

- `error: CTRL BPP2 reads 0`, or the menu is stretched to twice the width, in wrong colors: the bitstream has no 2 bits per pixel. The bitstream of this repository is loaded with `make bit` in `fw/`.
- Nothing on the uart: the user needs access to `/dev/ttyUSB1` (group `dialout`). The first line after a load is at times garbled.
- openFPGALoader or openocd do not find the board or the probe: access to the USB devices is needed (udev rules).
- After power off, the board starts the bitstream in its flash. The bitstream is loaded again with `make load`, or written to the flash with `make flash`.
- The display stays dark: the wiring is checked with `make border`, a colored line on each border.
