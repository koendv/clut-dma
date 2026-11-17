/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  main_host.c: the demo menu on the PC. A list of keys and changes of the
  status icons; after each the display is updated, a frame of st7789.py.

  usage: demo spi.txt names.txt
  names.txt: per frame the key, and how many tile rows were sent: 30 is the
  whole screen.
*/

#include <stdio.h>
#include "host.h"
#include "menu.h"
static uint8_t buf[U8G2_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t map[U8G2_MAP_SIZE];
static uint32_t clut[16][2] __attribute__((aligned(128)));

#define STATUS 255              /* not a key: new status icons */

static const struct
{
  uint8_t event;
  const char *name;
} keys[] = {
  { MENU_NEXT,   "next: Values" },
  { MENU_PREV,   "prev: Settings" },
  { MENU_SELECT, "select: the Settings form" },
  { MENU_SELECT, "select: edit the language" },
  { MENU_NEXT,   "next: Chinese" },
  { MENU_SELECT, "select: Chinese, the menu restarts" },
  { MENU_SELECT, "select: the settings form" },
  { MENU_NEXT,   "next: backlight" },
  { MENU_SELECT, "select: backlight off" },
  { MENU_NEXT,   "next: sleep" },
  { STATUS,      "status: battery low, no wifi, a warning" },
  { MENU_NEXT,   "next: back" },
  { MENU_SELECT, "select: main menu" },
  { MENU_NEXT,   "next: values" },
  { MENU_SELECT, "select: the values form" },
  { MENU_SELECT, "select: edit the brightness" },
  { MENU_NEXT,   "next: brightness 9" },
  { MENU_NEXT,   "next: brightness 10" },
  { MENU_PREV,   "prev: brightness 9" },
  { MENU_SELECT, "select: brightness done" },
  { MENU_NEXT,   "next: baud rate" },
  { MENU_SELECT, "select: edit the baud rate" },
  { MENU_NEXT,   "next: 38400" },
  { MENU_NEXT,   "next: 57600" },
  { MENU_SELECT, "select: baud rate done" },
  { MENU_NEXT,   "next: 1 stop bit" },
  { MENU_NEXT,   "next: 2 stop bits" },
  { MENU_SELECT, "select: 2 stop bits" },
  { MENU_PREV,   "prev: 1 stop bit" },
  { MENU_SELECT, "select: 1 stop bit" },
  { MENU_NEXT,   "next: 2 stop bits" },
  { MENU_NEXT,   "next: back" },
  { STATUS,      "status: charging" },
  { MENU_SELECT, "select: main menu, the cursor on values" },
  { MENU_NEXT,   "next: about" },
  { MENU_SELECT, "select: the about form, the main menu button" },
  { MENU_NEXT,   "next: the main menu button, the only field" },
  { MENU_SELECT, "select: main menu, by the button" },
};

static const menu_status_t status_change[] = {
  { 1, 2, 0, 1, 1 },
  { 8, 2, 0, 1, 0 },
};

int main(int argc, char **argv)
{
  u8g2_t u8g2;
  FILE *names;
  unsigned i, s = 0;

  if ( argc != 3 )
  {
    fprintf(stderr, "usage: demo spi.txt names.txt\n");
    return 1;
  }
  host_open(argv[1]);
  names = fopen(argv[2], "w");
  if ( names == NULL )
  {
    perror(argv[2]);
    return 1;
  }

  u8g2_Setup_st7789_240x280(&u8g2, buf, host_byte_cb, host_gpio_and_delay_cb);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFlipMode(&u8g2, 1);          /* connector at the top, as st7789.py shows it */
  menu_init(&u8g2, map, clut);
  u8g2_ClearDisplay(&u8g2);            /* also the 40 rows below the screen */
  fprintf(names, "clear display (%u tile rows)\n", host_frame());

  menu_draw(&u8g2);
  fprintf(names, "start: main menu (%u tile rows)\n", host_frame());
  for ( i = 0; i < sizeof keys / sizeof keys[0]; i++ )
  {
    if ( keys[i].event == STATUS )
      menu_status(&u8g2, &status_change[s++]);
    else
    {
      menu_event(keys[i].event);
      menu_draw(&u8g2);
    }
    fprintf(names, "%s (%u tile rows)\n", keys[i].name, host_frame());
  }
  fclose(names);
  host_close();
  return 0;
}
