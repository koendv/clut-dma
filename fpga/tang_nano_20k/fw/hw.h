/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  hw.h: registers of the Tang Nano 20K prototype, Gowin Cortex-M1 and
  clut_fabric. The addresses were measured over SWD: the Gowin IP sources are
  encrypted.
*/

#ifndef HW_H
#define HW_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define HCLK_HZ 54000000u

/* UART0: CMSDK APB UART, to the BL616 usb serial */
#define UART0_DATA      REG(0x50004000)
#define UART0_STATE     REG(0x50004004)   /* bit 0 tx full */
#define UART0_CTRL      REG(0x50004008)   /* bit 0 tx enable */
#define UART0_BAUDDIV   REG(0x50004010)

/* clut_dma, rtl/clut_dma.v */
#define DMA_CTRL        REG(0x60000000)   /* bit 0 START, bits 2:1 FORMAT, bit 3 IRQ_EN */
#define DMA_STATUS      REG(0x60000004)   /* bit 0 BUSY, bit 1 DONE (write 1 to clear) */
#define DMA_FB_START    REG(0x60000008)
#define DMA_MAP         REG(0x6000000C)
#define DMA_CLUT        REG(0x60000010)
#define DMA_LINE_OFFSET REG(0x60000014)
#define DMA_FIRST       REG(0x60000018)
#define DMA_SIZE        REG(0x6000001C)   /* bits 11:0 W, bits 27:16 H */

#define DMA_RGB565 0u
#define DMA_RGB444 1u
#define DMA_RGB666 2u

/* spi_lcd, fpga/rtl/spi_lcd.v */
#define SPI_DATA        REG(0x60000040)
#define SPI_CTRL        REG(0x60000044)
#define SPI_STATUS      REG(0x60000048)   /* bit 0 BUSY */
#define SPI_DIV         REG(0x6000004C)   /* sck = HCLK / (2 * DIV) */

#define SPI_DC     1u
#define SPI_CS_N   2u
#define SPI_RST_N  4u
#define SPI_BL     8u
#define SPI_STREAM 16u

/* gpio: bit 0 led (1 is on), bit 1 timing mark, bit 8 button S2 */
#define GPIO            REG(0x60000080)
#define GPIO_LED  1u
#define GPIO_MARK 2u
#define GPIO_S2   0x100u

/* fbmem: 16 kbyte, the memory clut_dma reads */
#define FBMEM 0x80000000u

/* SysTick */
#define SYST_CSR        REG(0xE000E010)
#define SYST_RVR        REG(0xE000E014)
#define SYST_CVR        REG(0xE000E018)

#endif
