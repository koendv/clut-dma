/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

#ifndef MENU_H
#define MENU_H

#include "u8g2.h"
#include "mui.h"
#include "mui_u8g2.h"

extern fds_t fds_en[];
extern fds_t fds_zh[];

enum { MENU_NEXT, MENU_PREV, MENU_SELECT };

/* the icons of the status row */
typedef struct
{
  uint8_t battery;              /* 0 .. 6 bars, 7 full, 8 charging */
  uint8_t signal;               /* 0 none, 1 .. 3 bars */
  uint8_t wifi;                 /* 0 none, 1 .. 4 */
  uint8_t bluetooth;            /* 0, 1 */
  uint8_t warning;              /* 0, 1 */
} menu_status_t;

/* the about form: shown when not 0. cpu name and clock, the code bytes and
   the times (us) are set by the application; menu_display_bytes, the
   buffer, the tile map and the clut, by the menu */
extern const char *menu_cpu_name;
extern uint32_t menu_cpu_mhz;
extern uint32_t menu_display_bytes;
extern uint32_t menu_code_bytes;
extern uint32_t menu_response_us;       /* the last partial update: a focus change, a select */
extern uint32_t menu_screen_us;         /* the last whole screen */

/* after the display setup; map and clut: memory the colors are read from
   (U8G2_MAP_SIZE bytes, 16 entries 128-byte aligned; fbmem for clut_dma) */
void menu_init(u8g2_t *u8g2, uint8_t *map, uint32_t (*clut)[2]);

/* a key: MENU_NEXT, MENU_PREV, MENU_SELECT; then menu_draw(). It marks the
   tile rows of the field that had the focus and of the field that has it
   for menu_draw() */
void menu_event(uint8_t event);

/* draw and send the tile rows marked by menu_event(); the whole screen, all
   pages, when mui asks for it (mui_UpdateWholeScreen(): a new form, a radio
   button) or after menu_update_whole_screen() */
void menu_draw(u8g2_t *u8g2);

/* the next menu_draw() sends the whole screen: after the application
   changed what a field shows, for a refresh, or back from a screen of its
   own. A field calls mui_UpdateWholeScreen(ui) instead */
void menu_update_whole_screen(void);

/* new icons; the status row drawn and sent */
void menu_status(u8g2_t *u8g2, const menu_status_t *status);

#endif
