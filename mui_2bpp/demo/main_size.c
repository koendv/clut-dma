/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  main_size.c: the demo menu for a Cortex-M1, without a board: what the
  library, mui, the menu and the font take in ITCM (make size). The SPI
  and the keys are stubs; the colors are expanded in software.
*/

#include "menu.h"

static uint8_t buf[U8G2_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t map[U8G2_MAP_SIZE];
static uint32_t clut[16][2] __attribute__((aligned(128)));
volatile uint8_t key, spi;

static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  uint8_t *data = (uint8_t *)arg_ptr;

  (void)u8x8;
  if ( msg == U8X8_MSG_BYTE_SEND )
    while ( arg_int-- > 0 )
      spi = *data++;
  return 1;
}

static uint8_t gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}

int main(void)
{
  u8g2_t u8g2;

  u8g2_Setup_st7789_240x280(&u8g2, buf, byte_cb, gpio_and_delay_cb);
  u8g2_InitDisplay(&u8g2);
  menu_init(&u8g2, map, clut);
  u8g2_ClearDisplay(&u8g2);
  menu_draw(&u8g2);
  for (;;)
  {
    menu_event(key);
    menu_draw(&u8g2);
  }
}
