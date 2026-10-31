# clut-dma

Color for a 1 bpp frame buffer, in hardware.

https://github.com/user-attachments/assets/8ba7157a-77d0-49f0-bef6-b4090baf7913

Demo of a soldering station. RISC-V, 24 MHz. SPI 12 MHz. 16 kB code, 2 kB RAM.

https://github.com/user-attachments/assets/165b180b-a101-4308-9619-e7595eb9faf1

Demo of a speed gauge. Arm Cortex-M1, 54 MHz. SPI 27 MHz. 15 kB code, 2 kB RAM.

https://github.com/user-attachments/assets/470c7089-7e80-47fd-9ec0-2cefbdd4fae9

Demo of the u8g2 and Adafruit GFX graphics libraries.

The display in the demos is an ST7789, 240×280.

# Description

The videos show a RISC-V and an Arm core, running inside a Tang Nano 20K FPGA.

The CPU draws a bitmap in black and white. A hardware DMA engine reads the bitmap from memory, and adds color while sending the image to the SPI display. No color frame buffer is needed. The graphics engine uses no multipliers, no block RAM.

The color is sufficient for a display on an industrial tool, but not for displaying photos or color gradients.

The advantage is being able to use easily sourced commodity displays on small microcontrollers with little RAM.

Memory for the display, in bytes:

| display | 16-bit frame buffer | bitmap + tile map + CLUT |
|---|---|---|
| 128×160 | 40,960 | 2,784 |
| 240×240 | 115,200 | 7,714 |
| 240×280 | 134,400 | 8,989 |

With a page buffer of one or two tile rows, the display data fits in about 1 kB ([details](clut-dma.md#integration)).

# Prototype

The deliverable is one Verilog file, [`rtl/clut_dma.v`](rtl/clut_dma.v).
Description in [`rtl/README.md`](rtl/README.md).

The engine is verified: invariants proven with SymbiYosys, output compared byte for byte with the u8g2 graphics library for 258 requests in simulation, and running on two FPGA builds.

Two FPGA bitstreams for the Tang Nano 20K run the same graphics engine:

- [Arm](fpga/tang_nano_20k_arm/README.md), Gowin Cortex-M1, 54 MHz
- [RISC-V](fpga/tang_nano_20k_riscv/README.md), Gowin GoRV32, 24 MHz

Both cores run the same demos.

# Publication

The methods are disclosed in a [defensive publication](https://doi.org/10.5281/zenodo.23835660) (working draft: [clut-dma.md](clut-dma.md)), so that they are prior art. The author does not seek patent rights on these methods, and covenants not to assert any patent covering them; see section 8 of the publication.

# License

Code: BSD-2-Clause. Text of the publication: CC BY 4.0.

## See also

The same color expansion in software, without the DMA engine:

- [u8g2, branch clut](https://github.com/koendv/u8g2/tree/clut): u8g2 with color drivers for ST7789, ILI9341 and GC9A01 displays, for Arduino.
  `u8x8_clut_stream_rgb565()` and `u8x8_clut_stream_rgb444()` in `csrc/u8x8_clut.c` are the software version of the engine.
- [micropython-u8g2](https://github.com/koendv/micropython-u8g2): u8g2 graphics and mui menus for MicroPython, with the same color drivers.
