/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  mui_test.c: a test of the partial updates, on the PC, with every field
  function of mui_u8g2.c, and what the demo does not have: lists that
  scroll, a goto to the form that is shown, a field that changes another
  field, radio buttons in different rows, leaving mui and coming back, a
  whole screen asked for by the application.

  The update rule is that of demo/menu.c (menu_event(), menu_draw()),
  without the status row. Every field gets the same keys; after each key a
  frame of ../fpga/tb/st7789.py. Built twice, with partial updates and with
  MENU_FULL_UPDATES (every update the whole screen): the frames must be the
  same. make test.

  usage: mui_test spi.txt names.txt
  names.txt: per frame the form, the field and the key, and the tile rows
  sent.

  ASCII only: the font of the demo.
*/

#include <stdio.h>
#include "host.h"
#include "mui.h"
#include "mui_u8g2.h"
#include "font_noto_sc_24.h"
#include "font_controls_28.h"

static uint8_t buf[U8G2_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t map[U8G2_MAP_SIZE];
static uint32_t clut[16][2] __attribute__((aligned(128)));

static u8g2_t u8g2;
static mui_t mui;

/*=== the variables of the fields ===*/

static uint8_t n_u8[4] = { 7, 7, 7, 7 }, n_x8[4] = { 0x2A, 0x2A, 0x2A, 0x2A };
static int8_t n_s8[4] = { -1, -1, -1, -1 };
static uint8_t bar[8] = { 30, 30, 30, 30, 30, 30, 30, 30 };
static uint8_t opt[4], chr = 'A', chk, radio;
static uint16_t list_line[2], list_sel, goto_sel;
static uint8_t parent[3];
static uint8_t exit_value;
static uint8_t other;

static const char *const list_names[] = { "alpha", "beta", "gamma", "delta", "omega" };

static const char *list_get(void *data, uint16_t index)
{
  (void)data;
  return index < 5 ? list_names[index] : "";
}

static uint16_t list_cnt(void *data)
{
  (void)data;
  return 5;
}

/* a goto list: the first character is the form */
static const char *const goto_names[] = { "\x0a" "Numbers", "\x0b" "Bars", "\x0c" "Options",
  "\x0d" "Lists", "\x01" "Test menu" };

static const char *goto_get(void *data, uint16_t index)
{
  (void)data;
  return index < 5 ? goto_names[index] : "\x01";
}

/* "T1": a checkbox whose value another field shows too, in other rows: it
   asks for the whole screen */
static uint8_t test_chkbox(mui_t *ui, uint8_t msg)
{
  uint8_t r = mui_u8g2_u8_chkbox_wm_pi(ui, msg);

  if ( msg == MUIF_MSG_CURSOR_SELECT || msg == MUIF_MSG_VALUE_INCREMENT || msg == MUIF_MSG_VALUE_DECREMENT )
    mui_UpdateWholeScreen(ui);
  return r;
}

/* "TR": the value of the checkbox, as text */
static uint8_t test_shown(mui_t *ui, uint8_t msg)
{
  if ( msg == MUIF_MSG_DRAW )
    u8g2_DrawUTF8(mui_get_U8g2(ui), mui_get_x(ui), mui_get_y(ui), other ? "on" : "off");
  return 0;
}

static muif_t muif_list[] = {
  MUIF_U8G2_FONT_STYLE(0, font_noto_sc_24),
  MUIF_U8G2_LABEL(),
  MUIF_RO("HR", mui_hline),
  MUIF_RO("GP", mui_u8g2_goto_data),
  MUIF_BUTTON("GC", mui_u8g2_goto_form_w1_pi),
  MUIF_BUTTON("GD", mui_u8g2_goto_form_w1_pf),
  MUIF_BUTTON("BK", mui_u8g2_btn_back_w1_pi),

  /* numbers */
  MUIF_U8G2_U8_MIN_MAX("N0", &n_u8[0], 0, 200, mui_u8g2_u8_min_max_wm_mse_pi),
  MUIF_U8G2_U8_MIN_MAX("N1", &n_u8[1], 0, 200, mui_u8g2_u8_min_max_wm_mud_pi),
  MUIF_U8G2_U8_MIN_MAX("N2", &n_u8[2], 0, 200, mui_u8g2_u8_min_max_wm_mse_pf),
  MUIF_U8G2_U8_MIN_MAX("N3", &n_u8[3], 0, 200, mui_u8g2_u8_min_max_wm_mud_pf),
  MUIF_U8G2_S8_MIN_MAX("N4", &n_s8[0], -9, 9, mui_u8g2_s8_min_max_wm_mse_pi),
  MUIF_U8G2_S8_MIN_MAX("N5", &n_s8[1], -9, 9, mui_u8g2_s8_min_max_wm_mud_pi),
  MUIF_U8G2_S8_MIN_MAX("N6", &n_s8[2], -9, 9, mui_u8g2_s8_min_max_wm_mse_pf),
  MUIF_U8G2_S8_MIN_MAX("N7", &n_s8[3], -9, 9, mui_u8g2_s8_min_max_wm_mud_pf),
  MUIF_U8G2_U8_MIN_MAX("N8", &n_x8[0], 0, 255, mui_u8g2_x8_min_max_wm_mse_pi),
  MUIF_U8G2_U8_MIN_MAX("N9", &n_x8[1], 0, 255, mui_u8g2_x8_min_max_wm_mud_pi),
  MUIF_U8G2_U8_MIN_MAX("NA", &n_x8[2], 0, 255, mui_u8g2_x8_min_max_wm_mse_pf),
  MUIF_U8G2_U8_MIN_MAX("NB", &n_x8[3], 0, 255, mui_u8g2_x8_min_max_wm_mud_pf),

  /* bars */
  MUIF_U8G2_U8_MIN_MAX_STEP("B0", &bar[0], 0, 60, 10, 0, mui_u8g2_u8_bar_wm_mse_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP("B1", &bar[1], 0, 60, 10, MUI_MMS_SHOW_VALUE, mui_u8g2_u8_bar_wm_mud_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP("B2", &bar[2], 0, 60, 10, MUI_MMS_NO_WRAP, mui_u8g2_u8_bar_wm_mse_pf),
  MUIF_U8G2_U8_MIN_MAX_STEP("B3", &bar[3], 0, 60, 10, 0, mui_u8g2_u8_bar_wm_mud_pf),
  MUIF_U8G2_U8_MIN_MAX_STEP_WIDTH("B4", &bar[4], 0, 40, 10, 60, 0, mui_u8g2_u8_fixed_width_bar_wm_mse_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP_WIDTH("B5", &bar[5], 0, 40, 10, 60, MUI_MMS_NO_WRAP,
      mui_u8g2_u8_fixed_width_bar_wm_mud_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP_WIDTH("B6", &bar[6], 0, 40, 10, 60, 0, mui_u8g2_u8_fixed_width_bar_wm_mse_pf),
  MUIF_U8G2_U8_MIN_MAX_STEP_WIDTH("B7", &bar[7], 0, 40, 10, 60, 0, mui_u8g2_u8_fixed_width_bar_wm_mud_pf),

  /* options, a character, a checkbox, radio buttons */
  MUIF_VARIABLE("O0", &opt[0], mui_u8g2_u8_opt_line_wa_mse_pi),
  MUIF_VARIABLE("O1", &opt[1], mui_u8g2_u8_opt_line_wa_mse_pf),
  MUIF_VARIABLE("O2", &opt[2], mui_u8g2_u8_opt_line_wa_mud_pi),
  MUIF_VARIABLE("O3", &opt[3], mui_u8g2_u8_opt_line_wa_mud_pf),
  MUIF_VARIABLE("CH", &chr, mui_u8g2_u8_char_wm_mud_pi),
  MUIF_VARIABLE("CB", &chk, mui_u8g2_u8_chkbox_wm_pi),
  MUIF_VARIABLE("RA", &radio, mui_u8g2_u8_radio_wm_pi),

  /* lists, parents and children */
  MUIF_U8G2_U16_LIST("L0", &list_line[0], NULL, list_get, list_cnt, mui_u8g2_u16_list_line_wa_mse_pi),
  MUIF_U8G2_U16_LIST("L1", &list_line[1], NULL, list_get, list_cnt, mui_u8g2_u16_list_line_wa_mud_pi),
  MUIF_VARIABLE("P0", &parent[0], mui_u8g2_u8_opt_parent_wm_pi),
  MUIF_VARIABLE("P1", &parent[1], mui_u8g2_u8_opt_parent_wm_pi),
  MUIF_VARIABLE("P2", &parent[2], mui_u8g2_u8_opt_parent_wm_pi),
  MUIF_VARIABLE("C0", &parent[0], mui_u8g2_u8_opt_radio_child_wm_pi),
  MUIF_VARIABLE("C1", &parent[1], mui_u8g2_u8_opt_radio_child_w1_pi),
  MUIF_VARIABLE("C2", &parent[2], mui_u8g2_u8_opt_child_wm_pi),
  MUIF_U8G2_U16_LIST("LP", &list_sel, NULL, list_get, list_cnt, mui_u8g2_u16_list_parent_wm_pi),
  MUIF_U8G2_U16_LIST("LC", &list_sel, NULL, list_get, list_cnt, mui_u8g2_u16_list_child_w1_pi),
  MUIF_U8G2_U16_LIST("LG", &goto_sel, NULL, goto_get, list_cnt, mui_u8g2_u16_list_goto_w1_pi),

  /* buttons */
  MUIF_BUTTON("G0", mui_u8g2_btn_goto_wm_fi),
  MUIF_BUTTON("G1", mui_u8g2_btn_goto_wm_if),
  MUIF_BUTTON("G2", mui_u8g2_btn_goto_w2_fi),
  MUIF_BUTTON("G3", mui_u8g2_btn_goto_w2_if),
  MUIF_BUTTON("G4", mui_u8g2_btn_goto_w1_pi),
  MUIF_BUTTON("G5", mui_u8g2_btn_goto_w1_fi),
  MUIF_BUTTON("K0", mui_u8g2_btn_back_wm_fi),
  MUIF_BUTTON("K1", mui_u8g2_btn_back_wm_if),
  MUIF_BUTTON("K2", mui_u8g2_btn_back_w2_fi),
  MUIF_BUTTON("K3", mui_u8g2_btn_back_w2_if),
  MUIF_BUTTON("K4", mui_u8g2_btn_back_w1_pi),
  MUIF_BUTTON("K5", mui_u8g2_btn_back_w1_fi),
  MUIF_VARIABLE("X0", &exit_value, mui_u8g2_btn_exit_wm_fi),
  MUIF_VARIABLE("X1", &exit_value, mui_u8g2_btn_exit_wm_if),
  MUIF_VARIABLE("X2", &exit_value, mui_u8g2_btn_exit_w2_fi),
  MUIF_VARIABLE("X3", &exit_value, mui_u8g2_btn_exit_w2_if),
  MUIF_VARIABLE("X4", &exit_value, mui_u8g2_btn_exit_w1_pi),
  MUIF_VARIABLE("X5", &exit_value, mui_u8g2_btn_exit_w1_fi),

  /* a field that changes another field */
  MUIF_VARIABLE("T1", &other, test_chkbox),
  MUIF_RO("TR", test_shown),
};

/*=== the forms: positions in tiles, as the demo (forms_en.c) ===*/

#define OPTIONS "one|two|three|four|five"

static fds_t fds[] =

/* a list that scrolls: 11 entries on 5 lines; "This form" is a goto to the
   form that is shown */
MUI_FORM(1)
MUI_STYLE(0)
MUI_LABEL(1, 5, "MUI TEST")
MUI_XY("HR", 0, 6)
MUI_DATA("GP",
    MUI_10 "Numbers|"
    MUI_11 "Bars|"
    MUI_12 "Options|"
    MUI_13 "Lists|"
    MUI_14 "Goto buttons|"
    MUI_15 "Back buttons|"
    MUI_16 "Exit buttons|"
    MUI_17 "Other field|"
    MUI_2 "Menu pf|"
    MUI_24 "Goto list|"
    MUI_1 "This form")
MUI_XYA("GC", 1, 9, 0)
MUI_XYA("GC", 1, 13, 1)
MUI_XYA("GC", 1, 17, 2)
MUI_XYA("GC", 1, 21, 3)
MUI_XYA("GC", 1, 25, 4)

MUI_FORM(2)
MUI_STYLE(0)
MUI_LABEL(1, 5, "MENU PF")
MUI_DATA("GP",
    MUI_10 "Numbers|"
    MUI_11 "Bars|"
    MUI_1 "Test menu")
MUI_XYA("GD", 1, 9, 0)
MUI_XYA("GD", 1, 13, 1)
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(10)
MUI_STYLE(0)
MUI_LABEL(1, 5, "NUMBERS")
MUI_XY("N0", 2, 9)
MUI_XY("N1", 12, 9)
MUI_XY("N2", 22, 9)
MUI_XY("N3", 2, 13)
MUI_XY("N4", 12, 13)
MUI_XY("N5", 22, 13)
MUI_XY("N6", 2, 17)
MUI_XY("N7", 12, 17)
MUI_XY("N8", 22, 17)
MUI_XY("N9", 2, 21)
MUI_XY("NA", 12, 21)
MUI_XY("NB", 22, 21)
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(11)
MUI_STYLE(0)
MUI_LABEL(1, 5, "BARS")
MUI_XY("B0", 1, 9)
MUI_XY("B1", 14, 9)
MUI_XY("B2", 1, 13)
MUI_XY("B3", 14, 13)
MUI_XY("B4", 1, 17)
MUI_XY("B5", 14, 17)
MUI_XY("B6", 1, 21)
MUI_XY("B7", 14, 21)
MUI_XYT("BK", 1, 29, "Back")

/* the radio buttons in different rows */
MUI_FORM(12)
MUI_STYLE(0)
MUI_LABEL(1, 5, "OPTIONS")
MUI_XYAT("O0", 2, 9, 96, OPTIONS)
MUI_XYAT("O1", 16, 9, 96, OPTIONS)
MUI_XYAT("O2", 2, 13, 96, OPTIONS)
MUI_XYAT("O3", 16, 13, 96, OPTIONS)
MUI_XY("CH", 2, 17)
MUI_XY("CB", 8, 17)
MUI_XYAT("RA", 16, 17, 0, "A")
MUI_XYAT("RA", 16, 21, 1, "B")
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(13)
MUI_STYLE(0)
MUI_LABEL(1, 5, "LISTS")
MUI_XYA("L0", 2, 9, 96)
MUI_XYA("L1", 16, 9, 96)
MUI_XYAT("P0", 2, 13, 20, OPTIONS)
MUI_XYAT("P1", 16, 13, 21, OPTIONS)
MUI_XYAT("P2", 2, 17, 22, OPTIONS)
MUI_XYA("LP", 16, 17, 23)
MUI_XYT("BK", 1, 29, "Back")

/* the children: 5 values on 3 lines */
MUI_FORM(20)
MUI_STYLE(0)
MUI_LABEL(1, 5, "RADIO CHILD WM")
MUI_XYA("C0", 2, 9, 0)
MUI_XYA("C0", 2, 13, 1)
MUI_XYA("C0", 2, 17, 2)

MUI_FORM(21)
MUI_STYLE(0)
MUI_LABEL(1, 5, "RADIO CHILD W1")
MUI_XYA("C1", 2, 9, 0)
MUI_XYA("C1", 2, 13, 1)
MUI_XYA("C1", 2, 17, 2)

MUI_FORM(22)
MUI_STYLE(0)
MUI_LABEL(1, 5, "CHILD WM")
MUI_XYA("C2", 2, 9, 0)
MUI_XYA("C2", 2, 13, 1)
MUI_XYA("C2", 2, 17, 2)

MUI_FORM(23)
MUI_STYLE(0)
MUI_LABEL(1, 5, "LIST CHILD")
MUI_XYA("LC", 2, 9, 0)
MUI_XYA("LC", 2, 13, 1)
MUI_XYA("LC", 2, 17, 2)

MUI_FORM(24)
MUI_STYLE(0)
MUI_LABEL(1, 5, "GOTO LIST")
MUI_XYA("LG", 1, 9, 0)
MUI_XYA("LG", 1, 13, 1)
MUI_XYA("LG", 1, 17, 2)
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(14)
MUI_STYLE(0)
MUI_LABEL(1, 5, "GOTO BUTTONS")
MUI_XYAT("G0", 8, 9, 15, "wm fi")
MUI_XYAT("G1", 22, 9, 16, "wm if")
MUI_XYAT("G2", 7, 13, 10, "w2 fi")
MUI_XYAT("G3", 22, 13, 11, "w2 if")
MUI_XYAT("G4", 1, 17, 12, "w1 pi")
MUI_XYAT("G5", 1, 21, 14, "w1 fi, this form")
MUI_XYT("BK", 1, 29, "Back")

MUI_FORM(15)
MUI_STYLE(0)
MUI_LABEL(1, 5, "BACK BUTTONS")
MUI_XYT("K0", 8, 9, "wm fi")
MUI_XYT("K1", 22, 9, "wm if")
MUI_XYT("K2", 7, 13, "w2 fi")
MUI_XYT("K3", 22, 13, "w2 if")
MUI_XYT("K4", 1, 17, "w1 pi")
MUI_XYT("K5", 1, 21, "w1 fi")

MUI_FORM(16)
MUI_STYLE(0)
MUI_LABEL(1, 5, "EXIT BUTTONS")
MUI_XYAT("X0", 8, 9, 0, "wm fi")
MUI_XYAT("X1", 22, 9, 1, "wm if")
MUI_XYAT("X2", 7, 13, 2, "w2 fi")
MUI_XYAT("X3", 22, 13, 3, "w2 if")
MUI_XYAT("X4", 1, 17, 4, "w1 pi")
MUI_XYAT("X5", 1, 21, 5, "w1 fi")

/* a checkbox and its value as text, in other rows */
MUI_FORM(17)
MUI_STYLE(0)
MUI_LABEL(1, 5, "OTHER FIELD")
MUI_LABEL(1, 9, "Value")
MUI_XY("T1", 25, 9)
MUI_LABEL(1, 13, "Shown")
MUI_XY("TR", 17, 13)
MUI_XYT("BK", 1, 29, "Back")

;

/*=== the update rule, as demo/menu.c ===*/

static uint8_t dirty_low = U8G2_TILE_ROWS - 1, dirty_high = 0;

static uint8_t test_dirty_field(fds_t *f)
{
  uint8_t cmd, y, r0, r1;

  if ( f == NULL )
    return 1;
  cmd = (uint8_t)(mui_get_fds_char(f) & 0xdf);
  if ( cmd == 'F' || cmd == 'B' || cmd == 'A' || cmd == 'T' )
    y = mui_get_fds_char(f + 4);
  else if ( cmd == 'G' || cmd == 'M' )
    y = mui_get_fds_char(f + 2);
  else
    return 0;
  u8g2_SetFont(&u8g2, font_noto_sc_24);
  u8g2_GetBarRows(&u8g2, (u8g2_uint_t)(y * MUI_U8G2_GRID), 1, &r0, &r1);
  if ( r0 > r1 )
    return 0;
  if ( r0 < dirty_low )
    dirty_low = r0;
  if ( r1 > dirty_high )
    dirty_high = r1;
  return 1;
}

static void test_event(char key)
{
  fds_t *focus = mui.cursor_focus_fds;
  uint16_t scroll_top = mui.form_scroll_top;

  if ( key == 'N' )
    mui_NextField(&mui);
  else if ( key == 'P' )
    mui_PrevField(&mui);
  else if ( key == 'S' )
    mui_SendSelect(&mui);
  else if ( key == '+' )
    mui_SendValueIncrement(&mui);
  else
    mui_SendValueDecrement(&mui);

  if ( mui.form_scroll_top != scroll_top || !test_dirty_field(focus) ||
      !test_dirty_field(mui.cursor_focus_fds) )
    mui_UpdateWholeScreen(&mui);
#ifdef MENU_FULL_UPDATES
  mui_UpdateWholeScreen(&mui);
#endif
}

static void test_draw(void)
{
  if ( !mui.update_whole_screen && dirty_low <= dirty_high )
    u8g2_FirstRows(&u8g2, dirty_low, dirty_high);
  else
    u8g2_FirstPage(&u8g2);
  do
  {
    mui_Draw(&mui);
  } while ( u8g2_NextPage(&u8g2) );
  mui.update_whole_screen = 0;
  dirty_low = U8G2_TILE_ROWS - 1;
  dirty_high = 0;
}

/*=== the keys ===*/

static FILE *names;

static void frame(const char *what, unsigned form, unsigned field, const char *key)
{
  fprintf(names, "%s %u field %u: %s (%u tile rows)\n", what, form, field, key, host_frame());
}

static void key(unsigned form, unsigned field, char k)
{
  char s[2] = { k, '\0' };

  test_event(k);
  test_draw();
  frame("form", form, field, s);
}

/* the application changes the form: the home key of an application */
static void go(unsigned form, unsigned field)
{
  mui_GotoForm(&mui, (uint8_t)form, (uint8_t)field);
  test_draw();
  frame("form", form, field, "goto");
}

/* the forms with fields of their own; the children (20 .. 23) are reached
   from their parents in form 13 */
static const uint8_t forms[] = { 1, 2, 10, 11, 12, 13, 24, 14, 15, 16, 17 };

/* every field: select; Next and Prev, a new value in edit mode, a list
   that scrolls, a focus change; select; and once more outside edit mode */
static const char field_keys[] = "SNNNNPPPPSNPS+-";

/* around a form and back: focus changes, lists that scroll and wrap */
static const char form_keys[] = "NNNNNNNNNNNNNPPPPPPPPPPPPP";

int main(int argc, char **argv)
{
  unsigned i, k, n;
  const char *p;

  if ( argc != 3 )
  {
    fprintf(stderr, "usage: mui_test spi.txt names.txt\n");
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
  u8g2_SetFlipMode(&u8g2, 1);
  u8g2_SetupClut2bpp(&u8g2, map, clut);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_STATUS, 0x212121, 0xEEEEEE);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_TITLE, 0x1565C0, 0xFFFFFF);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_CONTENT, 0x212121, 0xF5F5F5);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_BACK, 0x212121, 0xF5F5F5);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_INVERTED, 0xFFFFFF, 0x2196F3);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_EDIT, 0xFFFFFF, 0xF44336);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_BUTTON, 0xFFFFFF, 0x1976D2);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_INVERTED_CORNER, 0x2196F3, 0xF5F5F5);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_EDIT_CORNER, 0xF44336, 0xF5F5F5);
  u8g2_SetRoleColor(&u8g2, U8G2_ROLE_BUTTON_CORNER, 0x1976D2, 0xF5F5F5);
  u8g2_SetRolePalette(&u8g2, U8G2_ROLE_SLIDER, 0xF5F5F5, 0xE0E0E0, 0xE0E0E0, 0x2196F3);
  u8g2_SetControlFont(&u8g2, font_controls_28);
  u8g2_ClearDisplay(&u8g2);
  host_frame();
  fprintf(names, "clear display\n");

  mui_Init(&mui, &u8g2, fds, muif_list, sizeof muif_list / sizeof muif_list[0]);

  for ( i = 0; i < sizeof forms; i++ )
  {
    go(forms[i], 0);
    for ( p = form_keys; *p; p++ )
      key(forms[i], 0, *p);

    /* the fields of the form: from the first one Prev, to the last */
    mui_GotoForm(&mui, forms[i], 0);
    mui_PrevField(&mui);
    n = mui_GetCurrentCursorFocusPosition(&mui) + 1u;
    for ( k = 0; k < n; k++ )
    {
      go(forms[i], k);
      for ( p = field_keys; *p; p++ )
        key(forms[i], k, *p);
    }
  }

  /* leaving mui with tile rows marked, a screen of the application, and
     back: the form it was left from */
  go(16, 0);
  test_event('N');
  test_event('P');
  key(16, 0, 'S');
  u8g2_FirstPage(&u8g2);
  do
  {
    u8g2_SetFont(&u8g2, font_noto_sc_24);
    u8g2_DrawUTF8(&u8g2, 8, 120, "not mui");
  } while ( u8g2_NextPage(&u8g2) );
  frame("outside", 16, 0, "a screen of the application");
  dirty_low = 0;                /* tile rows marked while there is no form, as */
  dirty_high = 1;               /* new status icons in the demo */
  mui_RestoreForm(&mui);
  test_draw();
  frame("return", 16, 0, "back in mui");
  key(16, 0, 'N');

  /* the application changes what a field shows, and asks for the whole
     screen */
  go(17, 1);
  other = !other;
  mui_UpdateWholeScreen(&mui);
  test_draw();
  frame("form", 17, 1, "the application changes a value");
  key(17, 1, 'P');

  fclose(names);
  host_close();
  return 0;
}
