/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  forms_en.c: the menus of the demo in English. forms_zh.c has the same
  forms and fields in Chinese.

  Positions in units of 8x8 tiles (MUI_U8G2_GRID 8); widths in the arg of
  a field in pixels. The screen, 240x240, 30x30 tiles, in rows of 32
  pixels below a status row of 16 (u8g2.h, roles):

    tile rows  0 -  1   status (icons, drawn by menu.c)
               2 -  5   title, baseline y 5
               6 - 25   content, 5 rows, baselines y 9, 13, 17, 21, 25
              26 - 29   Back, baseline y 29

  The bar of a focused field is ascent 24 + descent 6 + 2 = 32 pixels high
  and covers the 4 tile rows of its row. Full width fields (menu lines,
  Back) have bars across the screen. The values end at x 224: an option
  line at x 16, 96 pixels wide, its text at the right, tiles 16 .. 27; the
  bar at x 17, 74 pixels and a margin of 6, tiles 17 .. 27; a checkbox at
  x 25, 24x24, tiles 25 .. 27. A radio button (RP) is a glyph at x, its
  text after it; a button of the fi style (BF) is centred on x, as wide as
  its text, in the button color.

  The characters of the strings make the font: make font.
*/

#include "menu.h"

fds_t fds_en[] =

MUI_FORM(1)
MUI_STYLE(0)
MUI_LABEL(1, 5, "MAIN MENU")
MUI_DATA("GP",
    MUI_10 "Settings|"
    MUI_20 "Values|"
    MUI_30 "About")
MUI_XYA("GC", 1, 9, 0)
MUI_XYA("GC", 1, 13, 1)
MUI_XYA("GC", 1, 17, 2)

MUI_FORM(10)
MUI_STYLE(0)
MUI_LABEL(1, 5, "SETTINGS")
MUI_LABEL(1, 9, "Language")
MUI_XYAT("LA", 17, 9, 96, "English|中文")
MUI_LABEL(1, 13, "Backlight")
MUI_XY("C1", 25, 13)
MUI_LABEL(1, 17, "Sleep")
MUI_XY("C2", 25, 17)
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(20)
MUI_STYLE(0)
MUI_LABEL(1, 5, "VALUES")
MUI_LABEL(1, 9, "Brightness")
MUI_XY("BR", 19, 9)
MUI_LABEL(1, 13, "Baud rate")
MUI_XYAT("SP", 16, 13, 96, "9600|19200|38400|57600|115200|230400|460800|921600")
MUI_LABEL(1, 17, "Stop bits")
MUI_XYAT("RP", 16, 17, 0, "1")
MUI_XYAT("RP", 23, 17, 1, "2")
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(30)
MUI_STYLE(0)
MUI_LABEL(1, 5, "ABOUT")
MUI_XYA("AB", 1, 9, 0)
MUI_XYAT("AB", 1, 13, 1, "VRAM")
MUI_XYAT("AB", 1, 17, 2, "Code")
MUI_XYAT("AB", 1, 21, 3, "Response")
MUI_XYAT("AB", 1, 25, 4, "Full screen")
MUI_XYAT("BF", 15, 29, 1, "Main menu")

;
