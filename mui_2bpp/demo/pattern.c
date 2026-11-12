/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  pattern.c: a test of the path from the 2 bpp bitmap to the display,
  without drawing: all four codes in every tile, three clut entries in the
  tile map, through u8x8_clut_stream_rgb444() and the ST7789 driver.

  usage: pattern spi.txt dump.bin
  dump.bin: the bitmap, the tile map and the clut, for check_pattern.py.
*/

#include <stdio.h>
#include <string.h>
#include "host.h"

static uint8_t buf[U8G2_BUF_SIZE];
static uint8_t map[(U8G2_TILE_COLS * U8G2_TILE_ROWS + 1) / 2];

/* {code 0 << 16 | code 1, code 2 << 16 | code 3}, rgb444 */
static const uint32_t clut[16][2] = {
  { 0x0000000FUL, 0x00F00F00UL },
  { 0x0FFF00FFUL, 0x0F0F0FF0UL },
  { 0x01230456UL, 0x07890ABCUL },
};

int main(int argc, char **argv)
{
  u8g2_t u8g2;
  FILE *f;
  int x, y, t;

  if ( argc != 3 )
  {
    fprintf(stderr, "usage: pattern spi.txt dump.bin\n");
    return 1;
  }
  host_open(argv[1]);
  u8g2_Setup_st7789_240x280(&u8g2, buf, host_byte_cb, host_gpio_and_delay_cb);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFlipMode(&u8g2, 1);          /* connector at the top, as st7789.py shows it */
  u8g2_ClearDisplay(&u8g2);
  host_frame();

  for ( t = 0; t < U8G2_TILE_COLS * U8G2_TILE_ROWS; t++ )
    map[t / 2] |= (uint8_t)(((t / U8G2_TILE_COLS + t % U8G2_TILE_COLS) % 3) << (4 * (t & 1)));
  u8g2_SetClutMap(&u8g2, map);
  u8g2_SetClutTable2bpp(&u8g2, clut);

  u8g2_FirstPage(&u8g2);
  do
  {
    /* the code of a pixel: its column plus its tile row, so every code at
       every position in a byte, and every tile has all four */
    for ( y = 0; y < U8G2_PAGE_ROWS * 8; y++ )
      for ( x = 0; x < U8G2_DISPLAY_WIDTH; x++ )
      {
        int yy = u8g2.page_row * 8 + y;
        int code = (x + yy / 8 + yy % 3) & 3;
        if ( yy < U8G2_DISPLAY_HEIGHT )
          buf[y * U8G2_LINE_STRIDE + x / 4] |= (uint8_t)(code << (6 - 2 * (x % 4)));
      }
  } while ( u8g2_NextPage(&u8g2) );
  host_frame();
  host_close();

  /* the whole bitmap, again: the page buffer holds only the last page */
  f = fopen(argv[2], "wb");
  if ( f == NULL )
  {
    perror(argv[2]);
    return 1;
  }
  for ( y = 0; y < U8G2_DISPLAY_HEIGHT; y++ )
    for ( x = 0; x < U8G2_LINE_STRIDE; x++ )
    {
      int b = 0, k;
      for ( k = 0; k < 4; k++ )
      {
        int xx = 4 * x + k;
        b |= ((xx + y / 8 + y % 3) & 3) << (6 - 2 * k);
      }
      fputc(b, f);
    }
  fwrite(map, 1, sizeof map, f);
  for ( t = 0; t < 16; t++ )
  {
    int w, k;
    for ( w = 0; w < 2; w++ )
      for ( k = 0; k < 4; k++ )
        fputc((int)((clut[t][w] >> (8 * k)) & 0xFF), f);
  }
  fclose(f);
  return 0;
}
