/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8x8_clut_dma.c: u8x8_clut_stream_rgb444() of the u8g2 branch clut, with
  clut_dma. The u8g2 ST7789 driver sets the window for a run of tiles of a
  tile row and calls this function; clut_dma reads the tiles, the tile map
  and the clut, expands the colors and sends the pixels to the SPI. The
  function replaces the software version in u8x8_clut.c, a weak symbol:
  link this file as an object, not from a library. Without USE_CLUT_DMA
  the file is empty and the CPU expands the colors.

  One display, one ST7789, one clut_dma. The page buffer, the tile map and
  the clut in memory clut_dma reads (fbmem); the clut 64-byte aligned. A
  clut table is required (u8g2_SetClutTable); without a tile map clut_dma
  uses entry 0 for every tile (NO_MAP), one word.
*/

#ifdef USE_CLUT_DMA

#include "u8x8.h"
#include <hw.h>                  /* the board's: -I. first, not this directory */

void u8x8_clut_stream_rgb444(u8x8_t *u8x8, const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number)
{
  uint32_t ctrl = DMA_FORMAT(DMA_RGB444) | DMA_VERT | DMA_START;

  while (SPI_STATUS & 1)         /* RAMWR sent */
    ;
  SPI_CTRL = SPI_CTRL | SPI_DC | SPI_STREAM;   /* data, from clut_dma */
  DMA_FB_START = (uint32_t)run_tile_ptr;
  if (u8x8->tile_clut_map) {
    DMA_MAP_START = (uint32_t)(u8x8->tile_clut_map + first_tile_number / 2u);
    DMA_MAP_NIBBLE = first_tile_number & 1u;   /* after MAP_START: its bit 0 */
  } else
    ctrl |= DMA_NO_MAP;
  DMA_CLUT = (uint32_t)u8x8->clut;
  DMA_LINE_STRIDE = 8u * u8x8->display_info->tile_width;
  DMA_SIZE = (8u << 16) | (8u * cnt);          /* one tile row */
  DMA_CTRL = ctrl;
  while (!(DMA_STATUS & 6))      /* DONE or ERR */
    ;
  DMA_STATUS = 2;                /* clear DONE; ERR stays, for the platform */
  while (SPI_STATUS & 1)         /* the last pixels */
    ;
  SPI_CTRL = SPI_CTRL & ~SPI_STREAM;
}

#endif
