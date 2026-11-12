/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  main_fig.c: the figures of the menu for the READMEs (make fig), on the PC:
  frames of the demo menu, as ../fpga/tb/st7789.py draws the bytes sent to
  the display.
    frame 0  the main menu, English                ../fig/mui2bpp-main.png
    frame 1  the values form, English, the focus   ../fig/mui2bpp-values.png
             on the second stop bits button
    frame 2  the same in Chinese                   ../fig/mui2bpp-values-zh.png
*/

#include <stdio.h>
#include "menu.h"
#include "host.h"

static uint8_t buf[U8G2_BUF_SIZE];
static uint8_t map[U8G2_MAP_SIZE];
static uint32_t clut[16][2];
static u8g2_t u8g2;

static void keys(const uint8_t *k, unsigned n)
{
  unsigned i;

  for ( i = 0; i < n; i++ )
  {
    menu_event(k[i]);
    menu_draw(&u8g2);
  }
}

int main(int argc, char **argv)
{
  /* the values form, the focus on the second stop bits button */
  static const uint8_t values[] = { MENU_NEXT, MENU_SELECT, MENU_NEXT, MENU_NEXT, MENU_NEXT };
  /* back to the main menu, settings, the language Chinese */
  static const uint8_t chinese[] = { MENU_NEXT, MENU_SELECT, MENU_PREV, MENU_SELECT,
      MENU_SELECT, MENU_NEXT, MENU_SELECT };

  if ( argc != 2 )
  {
    fprintf(stderr, "usage: fig spi.txt\n");
    return 1;
  }
  host_open(argv[1]);
  u8g2_Setup_st7789_240x280(&u8g2, buf, host_byte_cb, host_gpio_and_delay_cb);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFlipMode(&u8g2, 1);          /* connector at the top, as st7789.py shows it */
  menu_init(&u8g2, map, clut);
  u8g2_ClearDisplay(&u8g2);
  menu_draw(&u8g2);
  host_frame();
  keys(values, sizeof values);
  host_frame();
  keys(chinese, sizeof chinese);
  keys(values, sizeof values);
  host_frame();
  host_close();
  return 0;
}
