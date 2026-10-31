/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  hw.h: registers of the Tang Nano 20K RISC-V build, Gowin GoRV32 and
  clut_fabric_ahb. The GoRV32 addresses are from gorv32.h of the GoRV32 SDK.
*/

#ifndef HW_H
#define HW_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define HCLK_HZ 24000000u

/* UART1: 16550 registers at a 4 byte stride from 0x20, to the BL616 usb
   serial. the divisor assumes 16 times oversampling, as the SDK does */
#define UART1_RBR       REG(0xF0200020)   /* read: received byte */
#define UART1_THR       REG(0xF0200020)   /* write: byte to send */
#define UART1_DLL       REG(0xF0200020)   /* with LCR DLAB */
#define UART1_DLM       REG(0xF0200024)   /* with LCR DLAB */
#define UART1_FCR       REG(0xF0200028)
#define UART1_LCR       REG(0xF020002C)   /* bit 7 DLAB, bits 1:0 3: 8 bits */
#define UART1_LSR       REG(0xF0200034)   /* bit 0 data ready, bit 5 THR empty */

/* clut_dma, rtl/clut_dma.v, on the AHB slave port */
#define DMA_CTRL        REG(0xE8000000)   /* bit 0 START, bits 2:1 FORMAT, bit 3 IRQ_EN, bit 4 VERT, bit 5 NO_MAP */
#define DMA_STATUS      REG(0xE8000004)   /* bit 0 BUSY, bit 1 DONE, bit 2 ERR (write 1 to clear) */
#define DMA_FB_START    REG(0xE8000008)   /* bitmap: the first tile */
#define DMA_MAP_START   REG(0xE800000C)   /* map: the byte of the first tile's entry */
#define DMA_CLUT        REG(0xE8000010)
#define DMA_LINE_STRIDE REG(0xE8000014)   /* bitmap bytes from a line to the next */
#define DMA_MAP_NIBBLE  REG(0xE8000018)   /* bit 0: the entry in the high nibble */
#define DMA_SIZE        REG(0xE800001C)   /* bits 11:0 W, bits 27:16 H */

#define DMA_RGB565 0u
#define DMA_RGB444 1u
#define DMA_RGB666 2u
#define DMA_START  1u            /* DMA_CTRL bits */
#define DMA_FORMAT(f) ((f) << 1)
#define DMA_IRQ_EN 0x08u
#define DMA_VERT   0x10u
#define DMA_NO_MAP 0x20u

/* spi_lcd, fpga/rtl/spi_lcd.v, on the APB slave port */
#define SPI_DATA        REG(0xF8000040)
#define SPI_CTRL        REG(0xF8000044)
#define SPI_STATUS      REG(0xF8000048)   /* bit 0 BUSY */
#define SPI_DIV         REG(0xF800004C)   /* sck = HCLK / (2 * DIV) */

#define SPI_DC     1u
#define SPI_CS_N   2u
#define SPI_RST_N  4u
#define SPI_BL     8u
#define SPI_STREAM 16u

/* gpio: bit 0 led (1 is on), bit 1 timing mark, bit 8 button S2 */
#define GPIO            REG(0xF8000080)
#define GPIO_LED  1u
#define GPIO_MARK 2u
#define GPIO_S2   0x100u

/* fbmem: the memory clut_dma reads, an array in On-Chip RAM (main.c); the
   layout of testimg.h ends below 0x24C0. The soldering station demo: only
   the bytes it uses, FBMEM_USED of testimg.h */
#ifdef SOLDER_TEST
#include "testimg.h"
#define FBMEM_SIZE FBMEM_USED
#else
#define FBMEM_SIZE 0x24C0u
#endif
extern uint8_t fbmem[FBMEM_SIZE];
#define FBMEM ((uint32_t)fbmem)

#endif
