/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8x8_clut_2bpp.c: the colors of the 2 bpp frame buffer in software, for
  the u8x8 color drivers (u8x8_d_st7789.c), which set the display window of
  a run of tiles and call u8x8_clut_stream_rgb444(). This file replaces
  u8x8_clut.c of u8g2 branch clut; with clut_dma, a request with CTRL BPP2
  replaces u8x8_clut_stream_rgb444() instead.

  run_tile_ptr: the first byte of the run in the top pixel row of its tile
  row; the 8 pixel rows of the tiles are U8G2_LINE_STRIDE bytes apart, a
  tile is 2 bytes of each row (u8g2.h).

  The color of a pixel, as clut_dma computes it:
    idx  = the nibble of the tile in the tile map (0 without a map, and for
           the tiles of the panel below the screen)
    word = clut[idx][code >> 1]
    pix  = code odd: word bits 15:0, even: bits 31:16
  Without a clut: a gray ramp, black to white.
*/

#include "u8g2.h"
#include "u8x8_clut_driver.h"

/* the gray ramp: codes 0, 1, 2, 3 are 0x000, 0x555, 0xaaa, 0xfff */
static const uint32_t u8x8_clut_gray444[2] = { 0x00000555UL, 0x0AAA0FFFUL };

void u8x8_clut_set_native_bpp(u8x8_t *u8x8, uint8_t native_bpp)
{
  u8x8->clut_native_bpp = native_bpp;
}

static uint16_t u8x8_clut_color(const uint32_t *entry, uint8_t code)
{
  uint32_t word = entry[code >> 1];
  return (uint16_t)((code & 1) ? word : word >> 16);
}

void u8x8_clut_stream_rgb444(u8x8_t *u8x8, const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number)
{
  const uint32_t (*clut)[2] = u8g2_GetClutTable2bpp(u8x8);
  const uint8_t *map = clut ? u8x8->tile_clut_map : NULL;
  uint8_t buf[252];             /* 2 pixels -> 3 bytes, a multiple of 3 */
  uint8_t pos = 0;
  uint8_t row, i, k;

  for ( row = 0; row < 8; row++ )
  {
    const uint8_t *src = run_tile_ptr + (uint16_t)row * U8G2_LINE_STRIDE;

    for ( i = 0; i < cnt; i++ )
    {
      uint16_t tile = (uint16_t)(first_tile_number + i);
      uint8_t idx = (map && tile < U8G2_TILE_COLS * U8G2_TILE_ROWS) ? u8x8_clut_get_nibble(map, tile) : 0;
      const uint32_t *entry = clut ? clut[idx] : u8x8_clut_gray444;
      uint16_t px = (uint16_t)(src[2 * i] << 8 | src[2 * i + 1]);   /* 8 pixels, the first in bits 15:14 */

      for ( k = 0; k < 4; k++ )
      {
        uint16_t c0 = u8x8_clut_color(entry, (uint8_t)((px >> (14 - 4 * k)) & 3)) & 0x0FFF;
        uint16_t c1 = u8x8_clut_color(entry, (uint8_t)((px >> (12 - 4 * k)) & 3)) & 0x0FFF;

        /* ST7789 12 bit/pixel: 2 pixels in 3 bytes, R0G0 B0R1 G1B1 */
        buf[pos++] = (uint8_t)(c0 >> 4);
        buf[pos++] = (uint8_t)((c0 & 0x0F) << 4 | c1 >> 8);
        buf[pos++] = (uint8_t)c1;
        if ( pos == sizeof(buf) )
        {
          u8x8_cad_SendData(u8x8, pos, buf);
          pos = 0;
        }
      }
    }
  }
  if ( pos > 0 )
    u8x8_cad_SendData(u8x8, pos, buf);
}
