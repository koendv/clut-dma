/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8x8_clut_dma_2bpp.c: u8x8_clut_stream_rgb444() of mui_2bpp with clut_dma,
  2 bits per pixel (CTRL BPP2): the place of mui_2bpp/src/u8x8_clut_2bpp.c,
  which expands the colors in the cpu. The u8x8 ST7789 driver sets the
  window of a run of tiles of a tile row and calls this function; clut_dma
  reads the bitmap, the tile map and the clut and sends the pixels.

  run_tile_ptr: the first byte of the run in the top pixel row of the tile
  row; the rows are U8G2_LINE_STRIDE bytes apart. The tiles of the panel
  below the screen of the library (u8g2_ClearDisplay()) have no entry in
  the tile map: NO_MAP, clut entry 0. The buffer, map and clut in fbmem;
  the clut 128-byte aligned.

  Without USE_CLUT_DMA the file is empty.
*/

#ifdef USE_CLUT_DMA

#include "u8g2.h"
#include <hw.h>                  /* the board's: -I. first */

void u8x8_clut_stream_rgb444(u8x8_t *u8x8, const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number)
{
  uint32_t ctrl = DMA_FORMAT(DMA_RGB444) | DMA_BPP2 | DMA_START;

  while (SPI_STATUS & 1)         /* RAMWR sent */
    ;
  SPI_CTRL = SPI_CTRL | SPI_DC | SPI_STREAM;   /* data, from clut_dma */
  DMA_FB_START = (uint32_t)run_tile_ptr;
  if (u8x8->tile_clut_map && first_tile_number < U8G2_TILE_COLS * U8G2_TILE_ROWS) {
    DMA_MAP_START = (uint32_t)(u8x8->tile_clut_map + first_tile_number / 2u);
    DMA_MAP_NIBBLE = first_tile_number & 1u;   /* after MAP_START: its bit 0 */
  } else
    ctrl |= DMA_NO_MAP;
  DMA_CLUT = (uint32_t)u8g2_GetClutTable2bpp(u8x8);
  DMA_LINE_STRIDE = U8G2_LINE_STRIDE;
  DMA_SIZE = (8u << 16) | (8u * cnt);          /* one tile row */
  DMA_CTRL = ctrl;
  while (!(DMA_STATUS & 6))      /* DONE or ERR */
    ;
  DMA_STATUS = 2;                /* clear DONE; ERR stays */
  while (SPI_STATUS & 1)         /* the last pixels */
    ;
  SPI_CTRL = SPI_CTRL & ~SPI_STREAM;
}

void u8x8_clut_set_native_bpp(u8x8_t *u8x8, uint8_t native_bpp)
{
  u8x8->clut_native_bpp = native_bpp;
}

#endif
