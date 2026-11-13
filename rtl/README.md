# clut_dma.v

For the MCU designer. The deliverable of this repository is one Verilog file,
[`clut_dma.v`](clut_dma.v): a DMA engine that reads a 1 bpp bitmap, a tile
map and a color lookup table from memory, and sends RGB565, RGB444 or
RGB666 pixels straight to an SPI display. Its header comment is the
reference; this page is the overview. The bus adapters, the FPGA builds and
the test harnesses around it exist to show that this one file works.

## Description

Three areas in memory:

- a bitmap of 1 bit per pixel, in which the CPU draws;
- a tile map, with a 4-bit index for each 8×8 tile; not needed if the image is black and white (monochrome).
- a color lookup table (CLUT) of 16 foreground/background color pairs.

There is no color frame buffer anywhere.

The bitmap is in one of two layouts:

- row layout: a byte is 8 pixels of a pixel row, leftmost pixel in bit 7, as the 1-bit buffers of LVGL and Adafruit GFX;
- vertical layout, the tile layout of u8g2: a tile is 8×8 pixel, stored as 8 consecutive bytes.
  byte *c* is pixel column *c*, bit *r* is pixel row *r*, top pixel in bit 0.

A request is a rectangle that starts on a tile. A whole screen is one request. The CPU writes:

- FB_START: the bitmap address of the first tile.
  Row layout: the byte of the top left pixel, any address.
  Vertical layout: the first byte of the top left tile, 8-byte aligned;
  for u8g2 this is the tile pointer it passes to the display driver.
- MAP_START and MAP_NIBBLE: the byte of the tile map with the entry of the first tile, and which nibble (0 low, 1 high).
- If CTRL bit NO_MAP is set, there is no tile map: every tile has CLUT entry 0, and the engine reads no map.
  For a plain bitmap the CLUT is then one word, 64-byte aligned:
  0x0000FFFF for white on black, 0xFFFF0000 for black on white, in every RGB format.
- LINE_STRIDE: the bitmap bytes from a line to the next.
  In row layout from a pixel row to the next (the tiles per row).
  In vertical layout from a tile row to the next (8 × the tiles per row).
  The tile map has as many tiles per row as the bitmap; its stride follows from LINE_STRIDE.
- SIZE: width and height in pixels.
  The RTL supports displays where pixel rows or columns are not a multiple of 8 (e.g.: ILI9225: 220 rows).
  A last tile of fewer than 8 pixels is sent in part.
  RGB444 requires the width to be an even number of pixels.

With the Verilog parameter CACHE = 0 the engine reads the map, bitmap and CLUT words for every tile.
With CACHE = 1 (default) the engine reads them only when the next tile needs a different map, bitmap or CLUT word.
With CACHE = 0 the engine reads every word and may underrun the SPI at high wait states.
Use CACHE = 0 to tell a cache fault from a fault elsewhere.

The engine walks through the frame buffer and tile map, as a DMA channel steps its addresses.
For memory with less than 32 wait states, and with the default CACHE = 1, speed is limited by the SPI frequency.

In simulation (`make measure`: whole 240×240 screens, random tile map, SPI at the system clock, memory reads of 2 cycles):

- row layout: 10.9 word reads per tile, one read per 101 cycles, the bus busy about 2% of the cycles;
- vertical layout: 24.7 word reads per tile, one read per 45 cycles, the bus busy about 4.5% of the cycles.

On the boards, in the soldering station demo, color takes no time: a frame in color takes as long as in black and white, on both builds, within 1%.
The map and CLUT words are read beside the bitmap words, in the margin the SPI leaves; the SPI stays the limit.
The CPU draws in black and white either way; color only costs the RAM of the map and the CLUT
([ARM](../fpga/tang_nano_20k_arm/README.md#soldering-station-demo), [RISC-V](../fpga/tang_nano_20k_riscv/README.md#soldering-station-demo)).

## Registers

| offset | register | |
|---|---|---|
| 0x00 | CTRL | bit 0 START (write 1), bits 2:1 FORMAT (0 RGB565, 1 RGB444, 2 RGB666), bit 3 IRQ_EN, bit 4 VERT, bit 5 NO_MAP |
| 0x04 | STATUS | bit 0 BUSY, bit 1 DONE, bit 2 ERR; DONE and ERR: write 1 to clear |
| 0x08 | FB_START | bitmap address of the first tile |
| 0x0C | MAP_START | map byte with the entry of the first tile |
| 0x10 | CLUT | CLUT address, 64-byte aligned |
| 0x14 | LINE_STRIDE | bits 15:0 |
| 0x18 | MAP_NIBBLE | bit 0 |
| 0x1C | SIZE | bits 11:0 W, bits 27:16 H, in pixels |

As in a DMA channel, the engine updates FB_START, MAP_START, MAP_NIBBLE and H during a transfer; write them again for the next request.
ERR: a memory read failed. The engine stops reading, sends the bytes it already has, and sets ERR instead of DONE.

## Ports

clut_dma has no bus of its own. It has three simple ports, and a small adapter outside the module connects them to the bus of the MCU:

- register port: `reg_wr` and `reg_rd` are one-cycle strobes, with `reg_addr` (bits 4:2 of the offset) and `reg_wdata`.
  A write takes effect in the cycle of `reg_wr`; `reg_rdata` is valid in the cycle after `reg_rd`. No wait states, no errors. `irq` = (DONE or ERR) and IRQ_EN, level, from a flip-flop; the CPU reads the cause in STATUS, so the IRQ line can be shared.
- memory read port: one word read at a time. `rd_addr` is stable while `rd_req` is high; `rd_ack` comes with `rd_data`, and with `rd_err` if the read failed.
- byte stream: `byte_valid`, `byte_data`, `byte_ready`, to a byte-wide SPI master.

All outputs come from flip-flops.

## Size

A quick estimate: synthesized with Yosys to generic 2-input gates (`make size`):

| | flip-flops | gates | gate equivalents |
|---|---|---|---|
| CACHE = 1 (default) | 409 | 1,623 | 5,376 |
| CACHE = 0 | 345 | 1,467 | 4,654 |

Gate equivalents: NAND2 = 1, weights in `tb/ge.awk`.

A rough estimate, from a synthesis to an open 180 nm cell library: clut_dma, including the bus connections, is about 7,000 gate equivalents (GF180MCU 7-track cells, yosys and OpenROAD, ABC in area mode, the Hazard3 example SoC with an SPI and clut_dma added, synthesis only).
`clut_dma.v` synthesizes to these standard cells unchanged.
No RAM: no frame buffer, no line buffer, no CLUT storage.
On the Gowin GW2AR-18 of the ARM build (`fpga/tang_nano_20k_arm/size.sh`): 638 LUTs, 84 ALUs, 385 flip-flops, no block RAM.

No multipliers, no memories. One clock, asynchronous active-low reset, all outputs from flip-flops.

## In an MCU

A small graphics engine for two kinds of MCU:
the lowest-cost parts, with 2 to 3 kB of RAM, where no color frame buffer fits;
and value MCUs with real-time tasks, where the CPU belongs to the control loop, not to the display.

### Connections

clut_dma has three connections; two of them decide the cost of the integration.

- Register slave. Cheap: for APB the adapter is wiring only (`apb_reg_slave.v`).
- Read master. A master port on the bus matrix: a port on the arbiter, a path to each memory it reads, and its place in the security and privilege attributes of the system (TrustZone, MPU).
  As delivered, clut_dma has a read port; in the RISC-V build an AHB-Lite master adapter (`ahb_rd_master.v`) connects it to a master port of its own.
  Built as a mode of an existing DMA channel, it uses the master port, the address registers, the interrupt and the request logic of that channel;
  about 165 of the 409 flip-flops are configuration registers, part of which the channel already has.
- Byte stream to the SPI. Not a bus port: in the prototypes it goes straight into the transmit path of the SPI (`spi_lcd.v`), without bus traffic per byte.
  In an MCU this is a stream input on the transmit path of the SPI peripheral, a small multiplexer.
  The alternative, the engine writing each byte to the SPI data register over the bus, is one bus write per byte: at an SPI of 27 MHz, 3.4 million writes per second, far more bus traffic than the reads.
  If the SPI takes 16-bit frames, half of that.

The cheapest integration is the engine inside the DMA channel, with its output into the SPI. That integration is described in the publication (section 4.6, variants 5 and 13), not built: the prototypes have a separate engine with its own master.

### Real-time properties

By construction of `clut_dma.v`:

- No CPU time during a transfer. Per request the CPU sets the display window and writes about six registers; one interrupt at the end. The CPU work depends on the number of requests, not on the number of pixels.
- One read outstanding: single word reads, no bursts. A bus master of higher priority waits at most for the one read in progress.
- The read rate is bounded by the SPI. The fetch stage runs at most one tile ahead of the output: at most 3 reads (row layout) or 4 (vertical layout) per tile and pixel row sent. On average far less: one read per 101 cycles (row layout) or 45 (vertical layout) with the SPI at the system clock (see Description).
- Read only. The engine never writes memory.
- Lowest bus priority is allowed. A read that waits can cause an SPI underrun: the transfer takes longer, the bytes are the same.
- A failed read stops the reads; the bytes already fetched go out, and ERR is set instead of DONE.

When the application needs the whole CPU, the transfer in flight completes; only the next request waits. With page buffers, a long absence of the CPU in the middle of a frame shows: the upper tile rows are new, the lower ones still old, until the CPU starts the next request.

### Timing

The longest path runs from the fetch state through the map adder, the nibble multiplexer of the map word and the compare of the CLUT index: about 16 ns on the GW2AR-18, Fmax about 60 MHz, at 54 MHz with 2.5 ns slack, about the same as the worst path of the Cortex-M1 in the same design.
In the same synthesis, without wires, the longest path inside clut_dma is about half the longest path inside the Hazard3 CPU; paths through the bus are not measured.

### Limits

- W and H up to 4,095 pixels; LINE_STRIDE up to 65,535 bytes.
- CLUT 64-byte aligned; map and row-layout bitmap at any byte, vertical-layout bitmap 8-byte aligned.
- RGB565, RGB444 (W even) and RGB666.
- 1 bit per pixel. The anti-aliasing variants of the publication (2 bits per pixel, two bit planes) are not built.

### Status

Done: formal invariants of the engine; synthesis to GF180MCU standard cells, alone and in a small RISC-V MCU; the bus adapters proven against APB3 and AHB-Lite properties; simulation against u8g2 for 258 requests, with random memory latency and output stalls; two FPGA integrations; a u8g2 display driver (vertical layout) and an Adafruit GFX example (row layout); NO_MAP for a plain 1-bit bitmap, such as LVGL I1.

Not done: the integration as a mode of a DMA channel and as a stream input of an SPI peripheral; clock gating when idle (then only the register port needs a clock); placement and routing in a standard cell library; scan insertion.

## Two bitstreams

The same `clut_dma.v` runs in two FPGA builds on a Tang Nano 20K (Gowin GW2AR-18). Only the connection around it differs, because the two soft cores offer different ports.

| | [ARM](../fpga/tang_nano_20k_arm/README.md) | [RISC-V](../fpga/tang_nano_20k_riscv/README.md) |
|---|---|---|
| CPU | Gowin Cortex-M1, 54 MHz | Gowin GoRV32, 24 MHz |
| registers | APB, `apb_reg_slave.v` | AHB-Lite slave, `ahb_reg_slave.v` |
| memory reads | second port of a dual-port block RAM, `fbmem.v` | CPU On-Chip RAM, AHB-Lite master `ahb_rd_master.v` |
| frame buffer | 16 KiB fbmem, the CPU writes it over AHB | in the 32 KiB On-Chip RAM, with program and data |

ARM: the Cortex-M1 on this FPGA has few bus ports, one APB and one AHB slot, and no port through which a peripheral can read CPU RAM.
So the frame buffer is a dual-port block RAM on the AHB slot: the CPU writes it over AHB, clut_dma reads the second port.
`rd_err` is tied low.

RISC-V: GoRV32 has an AHB slave port, an AHB master port into its memory, and APB.
The registers are on the AHB slave; clut_dma reads the frame buffer, tile map and CLUT from CPU RAM through its own AHB-Lite master, as it would in an MCU.
The limits are those of the core on this FPGA: Fmax is about 35 MHz, and the 64 KB On-Chip RAM configuration is faulty, so the build runs at 24 MHz with 32 KiB for program and data.

The publication ([clut-dma.pdf](../clut-dma.pdf), source [clut-dma.md](../clut-dma.md)) describes this file.

## Bus adapters and test harnesses

Bus adapters, small and outside the IP:

- `fpga/tang_nano_20k_arm/rtl/apb_reg_slave.v`: APB3, wiring only.
- `fpga/tang_nano_20k_riscv/rtl/ahb_reg_slave.v`: AHB-Lite slave, one wait state on a read, ERROR on a byte or halfword transfer.
- `fpga/tang_nano_20k_riscv/rtl/ahb_rd_master.v`: AHB-Lite master, single word reads, HRESP to `rd_err`.

Tests of `clut_dma.v`:

- `tb/`: testbench. `ref.c` writes the memory and the requests, and the
  bytes u8g2 `u8x8_clut_stream_rgb565()` and `u8x8_clut_stream_rgb444()`
  send for them; `tb_clut_dma.v` compares the model with those bytes, and
  checks the number of memory reads and the registers at the end. `ge.awk`
  turns a yosys gate count into gate equivalents, for `make size`.
- `rtl/clut_dma.v`: its `ifdef FORMAL` block holds the invariants of the engine.
- `formal/`: SymbiYosys.
  - `clut_dma.sby`: the invariants. Its covers of the vertical layout take long and run in `nightly.sh` (tasks `cover_vert`, `cover_vert0`).
  - `apb.sby`: clut_dma with `apb_reg_slave`, and spi_lcd, against APB3 slave properties (`fapb3_slave.v`).
  - `ahb.sby`: `ahb_reg_slave` and `ahb_rd_master` against AHB-Lite slave and master properties (`fahbl_slave.v`, `fahbl_master.v`).
  - `fbmem.sby`: the contents of fbmem.
- `fpga/tb/`: `tb_fabric.v`, testbench of the ARM fabric as the CPU uses it;
  `tb_frame.v` and `st7789.py`, the simulated display image of a firmware test.
- On the boards: the firmware of both builds, see the demo pages.

## Contents

- `rtl/clut_dma.v`: the DMA engine.
- `tb/`, `formal/`: tests, above.
- `fpga/rtl/`: shared by both builds: `spi_lcd.v` (SPI master for the
  display, with backlight PWM) and `fbmem.v` (dual-port memory, AHB-Lite for
  the CPU, a read port for clut_dma; ARM only).
- `fpga/tang_nano_20k_arm/`: the Cortex-M1 build. `rtl/clut_fabric.v`
  (clut_dma, fbmem and spi_lcd on one APB and one AHB slot),
  `rtl/apb_reg_slave.v`, top level, constraints, Gowin project, `fw/`.
  `size.sh`: the size of clut_dma in the FPGA. `fw/solder.c`: the
  soldering station demo, for both builds; `fw/font/`: its 24×24 font and
  the script that makes it; `fw/solder_demo.sh`: the demo with keys, for a
  video.
- `fpga/tang_nano_20k_riscv/`: the GoRV32 build. `rtl/clut_fabric_ahb.v`
  (clut_dma and spi_lcd, registers on AHB, reads over AHB, spi_lcd on APB),
  `rtl/ahb_reg_slave.v`, `rtl/ahb_rd_master.v`, top level, constraints,
  Gowin project, `fw/`.
- `fpga/measure_solder.sh`: the soldering station demo in every variant,
  measured on the board.
- `fpga/size/`: stubs of clut_dma and clut_fabric, for `size.sh`.
- `fpga/tb/`: tests of the fabric and of the firmware, above.
- `nightly.sh`: the overnight tests, on a snapshot of the last commit.
- `clut-dma.pdf`: the publication, as on Zenodo. `clut-dma.md`: its source.
  `fig/`: its two figures, SVG, and two frames of the soldering station demo, PNG.

## Requirements

- OSS CAD Suite (tested 2026-10-28: Yosys 0.70, SymbiYosys with yices,
  Icarus Verilog 14, Verilator 5.053), on the PATH.
- u8g2 with the color drivers, branch
  [clut](https://github.com/koendv/u8g2/tree/clut) of koendv/u8g2, in
  `../u8g2` (or `make U8G2=...`).
- A C compiler, for `tb/ref.c` and `fw/host.c`; a C++ compiler for `make sim
  TEST=stars`.
- Adafruit GFX Library (tested 1.13.0), for `make stars`, in
  `~/Arduino/libraries/Adafruit_GFX_Library` (or `make ADAFRUIT_GFX=...`).
- PDF: pandoc, xelatex, Noto Serif CJK SC, rsvg-convert (for the figures).
- The FPGA builds need more: Gowin EDA, openFPGALoader, a cross compiler and
  OpenOCD. See the demo pages.

## Use

In the top directory:

```
make test          # simulate, compare with u8g2; also the read counts and registers
make test-nocache  # the same with CACHE = 0
make ahb           # the tests of make test behind the AHB-Lite adapters, and a bus error
make fabric        # the ARM clut_fabric as the cpu uses it, with the display window commands
make lint          # verilator lint of all rtl
make size          # yosys flip-flops, gates and gate equivalents, CACHE = 1 and 0
make formal        # SymbiYosys, the short tasks, about 8 minutes
make measure       # spi underrun for spi clock and memory wait states, row and vertical layout, about 40 minutes;
                   # make measure WAIT_STATES="16 24 32" for external memory
make pdf           # clut-dma.pdf from clut-dma.md, README.pdf
make zip           # build/clut-dma.zip, the files of the last commit
make binzip        # build/clut-dma-bin-<commit>.zip: the bitstreams and firmware of both builds
./nightly.sh &     # the long tests, results in build/nightly/latest/summary.txt
```

`nightly.sh` also builds the firmware of both boards and runs the firmware
simulations (`make sim`) of the ARM build.
