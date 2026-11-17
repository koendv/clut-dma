/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  menu.c: the demo menu, the same on the PC and on a board: the fields of
  the forms, the colors of the roles, the status row, and the switch
  between English and Chinese, which restarts the menu in the other
  language (in arm_can_tool: a reboot).

  Colors: the library fills the tile map and the clut (u8g2.h, roles); here
  only the colors of the roles, set once. The field with the focus is a
  color, the inverted role, the field being edited the edit role, a button
  the button role; the back row has the colors of the content. The
  colors are those of the light theme of LVGL (lv_theme_default, the
  palette of Material Design): the screen F5F5F5, the text 212121, the
  focus blue 2196F3, the edit red F44336; the title a darker blue, 1565C0,
  for contrast on white. The bar field is the slider of LVGL, the bars have
  rounded corners. Dark text on a light background takes gamma
  U8G2_GAMMA_DARK (u8g2.h), else it would look thin.
  Checkboxes, radio buttons and the mark of a list are glyphs of the
  control font.

  Updates: a key redraws and sends only the tile rows of the field that had
  the focus and of the field that has it: one span, low .. high. A focus
  change is two fields; a select or a new value in a field is one. New
  status icons: only the status row. The whole screen when mui says that
  more changed (mui_UpdateWholeScreen() in mui.h: a new form, a radio button, a
  field of the application that changes other fields), when a list scrolls,
  and after menu_update_whole_screen(). MENU_FULL_UPDATES: every update the whole
  screen, the reference.
*/

#include "menu.h"
#include "font_noto_sc_24.h"
#include "font_icons_20.h"
#include "font_controls_28.h"
#include "icons.h"

static mui_t mui;
static uint8_t language, shown_language;      /* 0 English, 1 Chinese */
static uint8_t backlight = 1, sleep_enable, brightness = 8, speed = 1, stop_bits;
static menu_status_t status = { 5, 3, 3, 1, 0 };

/* the tile rows menu_draw() updates, when mui does not ask for the whole
   screen (mui.update_whole_screen); empty (low > high): the whole screen */
static uint8_t dirty_low = U8G2_TILE_ROWS - 1, dirty_high = 0;

#if U8G2_STATUS_ROWS < 1
#error "menu.c: the status row needs at least one tile row"
#endif

static void menu_dirty_rows(uint8_t r0, uint8_t r1)
{
  if ( r0 < dirty_low )
    dirty_low = r0;
  if ( r1 > dirty_high )
    dirty_high = r1;
}

/* the whole screen: the flag of mui, cleared by menu_draw(). Tile rows
   marked before or after do not make it a partial update */
void menu_update_whole_screen(void)
{
  mui_UpdateWholeScreen(&mui);
}

/* after a draw: nothing marked */
static void menu_dirty_clear(void)
{
  dirty_low = U8G2_TILE_ROWS - 1;
  dirty_high = 0;
}

/*=== the about form: numbers of the build and, with STATS, measured ===*/

const char *menu_cpu_name;
uint32_t menu_cpu_mhz;
uint32_t menu_display_bytes = U8G2_BUF_SIZE + U8G2_MAP_SIZE + 16 * 8;
uint32_t menu_code_bytes;
uint32_t menu_response_us;
uint32_t menu_screen_us;

static char *menu_str(char *p, const char *s)
{
  while ( *s )
    *p++ = *s++;
  return p;
}

static char *menu_utoa(char *p, uint32_t v)
{
  char d[10];
  uint8_t n = 0;

  do
  {
    d[n++] = (char)('0' + v % 10);
    v /= 10;
  } while ( v );
  while ( n )
    *p++ = d[--n];
  return p;
}

/* KB of 1024 bytes and a tenth, by shifts: 2498 bytes 2.4 KB */
static char *menu_kb(char *p, uint32_t b)
{
  p = menu_utoa(p, b >> 10);
  *p++ = '.';
  p = menu_utoa(p, ((b & 1023) * 10) >> 10);
  return menu_str(p, " KB");
}

/* whole ms, rounded */
static char *menu_ms(char *p, uint32_t us)
{
  p = menu_utoa(p, (us + 500) / 1000);
  return menu_str(p, " ms");
}

/* MUI_XYA("AB", x, y, arg) or MUI_XYAT("AB", x, y, arg, label): a line of
   the about form, the label and a number. arg: 0 the cpu and its clock,
   1 the memory for the display, 2 the code, 3 the last partial update, 4 the
   last whole screen. Nothing when the number is not known (0) */
static uint8_t menu_about_line(mui_t *ui, uint8_t msg)
{
  char s[48], *p = s;

  if ( msg != MUIF_MSG_DRAW )
    return 0;
  if ( ui->text[0] )
  {
    p = menu_str(p, ui->text);
    *p++ = ' ';
  }
  switch ( ui->arg )
  {
    case 0:
      if ( menu_cpu_name == NULL )
        return 0;
      p = menu_str(p, menu_cpu_name);
      *p++ = ' ';
      p = menu_utoa(p, menu_cpu_mhz);
      p = menu_str(p, " MHz");
      break;
    case 1:
      if ( menu_display_bytes == 0 )
        return 0;
      p = menu_kb(p, menu_display_bytes);
      break;
    case 2:
      if ( menu_code_bytes == 0 )
        return 0;
      p = menu_kb(p, menu_code_bytes);
      break;
    case 3:
      if ( menu_response_us == 0 )
        return 0;
      p = menu_ms(p, menu_response_us);
      break;
    case 4:
      if ( menu_screen_us == 0 )
        return 0;
      p = menu_ms(p, menu_screen_us);
      break;
    default:
      return 0;
  }
  *p = '\0';
  u8g2_DrawUTF8(mui_get_U8g2(ui), mui_get_x(ui), mui_get_y(ui), s);
  return 0;
}

static muif_t muif_list[] = {
  MUIF_U8G2_FONT_STYLE(0, font_noto_sc_24),
  MUIF_U8G2_LABEL(),
  MUIF_RO("GP", mui_u8g2_goto_data),
  MUIF_BUTTON("GC", mui_u8g2_goto_form_w1_pi),
  MUIF_VARIABLE("LA", &language, mui_u8g2_u8_opt_line_wa_mud_pi),
  MUIF_VARIABLE("C1", &backlight, mui_u8g2_u8_chkbox_wm_pi),
  MUIF_VARIABLE("C2", &sleep_enable, mui_u8g2_u8_chkbox_wm_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP_WIDTH("BR", &brightness, 0, 16, 1, 74, MUI_MMS_NO_WRAP,
      mui_u8g2_u8_fixed_width_bar_wm_mud_pi),
  MUIF_VARIABLE("SP", &speed, mui_u8g2_u8_opt_line_wa_mud_pi),
  MUIF_VARIABLE("RP", &stop_bits, mui_u8g2_u8_radio_wm_pi),
  MUIF_BUTTON("BF", mui_u8g2_btn_goto_wm_fi),
  MUIF_BUTTON("BK", mui_u8g2_btn_back_w1_pi),
  MUIF_RO("AB", menu_about_line),
};

static void menu_fds(void)
{
  mui_Init(&mui, mui.graphics_data, language ? fds_zh : fds_en, muif_list,
      sizeof muif_list / sizeof muif_list[0]);
  shown_language = language;
}

void menu_init(u8g2_t *u8g2, uint8_t *map, uint32_t (*clut)[2])
{
  u8g2_SetupClut2bpp(u8g2, map, clut);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_STATUS, 0x212121, 0xEEEEEE);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_TITLE, 0x1565C0, 0xFFFFFF);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_CONTENT, 0x212121, 0xF5F5F5);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_BACK, 0x212121, 0xF5F5F5);    /* as the content */
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_INVERTED, 0xFFFFFF, 0x2196F3);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_EDIT, 0xFFFFFF, 0xF44336);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_BUTTON, 0xFFFFFF, 0x1976D2);
  /* the rounded corners of the bars: the bar color on the screen color */
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_INVERTED_CORNER, 0x2196F3, 0xF5F5F5);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_EDIT_CORNER, 0xF44336, 0xF5F5F5);
  u8g2_SetRoleColor(u8g2, U8G2_ROLE_BUTTON_CORNER, 0x1976D2, 0xF5F5F5);
  /* the bar field as the slider of LVGL: the screen, the track, -, the indicator */
  u8g2_SetRolePalette(u8g2, U8G2_ROLE_SLIDER, 0xF5F5F5, 0xE0E0E0, 0xE0E0E0, 0x2196F3);
  u8g2_SetControlFont(u8g2, font_controls_28);  /* checkbox, radio button, the mark of a list */

  mui.graphics_data = u8g2;
  menu_fds();
  mui_GotoForm(&mui, 1, 0);
}

/*=== status row ===*/

static const char *const battery_icon[] = {
  ICON_BATTERY_0_BAR, ICON_BATTERY_1_BAR, ICON_BATTERY_2_BAR, ICON_BATTERY_3_BAR,
  ICON_BATTERY_4_BAR, ICON_BATTERY_5_BAR, ICON_BATTERY_6_BAR, ICON_BATTERY_FULL,
  ICON_BATTERY_CHARGING_FULL,
};
static const char *const signal_icon[] = {
  "", ICON_SIGNAL_CELLULAR_ALT_1_BAR, ICON_SIGNAL_CELLULAR_ALT_2_BAR, ICON_SIGNAL_CELLULAR_ALT,
};
static const char *const wifi_icon[] = {
  "", ICON_NETWORK_WIFI_1_BAR, ICON_NETWORK_WIFI_2_BAR, ICON_NETWORK_WIFI_3_BAR, ICON_WIFI,
};

/* the icons: 16 pixels of ink, from 18 rows above the baseline; away from
   the rounded corners of the panel: x 28 .. 211 */
static void menu_draw_status(u8g2_t *u8g2)
{
  const u8g2_uint_t y = 18;

  u8g2_SetFont(u8g2, font_icons_20);
  u8g2_DrawUTF8(u8g2, 28, y, signal_icon[status.signal % 4]);
  u8g2_DrawUTF8(u8g2, 52, y, wifi_icon[status.wifi % 5]);
  if ( status.warning )
    u8g2_DrawUTF8(u8g2, 144, y, ICON_WARNING);
  if ( status.bluetooth )
    u8g2_DrawUTF8(u8g2, 168, y, ICON_BLUETOOTH);
  u8g2_DrawUTF8(u8g2, 192, y, battery_icon[status.battery % 9]);
}

void menu_status(u8g2_t *u8g2, const menu_status_t *s)
{
  status = *s;
#ifdef MENU_FULL_UPDATES
  menu_update_whole_screen();
#else
  menu_dirty_rows(0, U8G2_STATUS_ROWS - 1);   /* the status row, not a field */
#endif
  menu_draw(u8g2);
}

/*=== keys ===*/

/* the tile rows of a field: the rows of its bar, as the focus marks them.
   y in units of MUI_U8G2_GRID, as mui_prepare_current_field() reads it:
   byte 4 of F, B, A, T (MUI_XY, MUI_XYT, MUI_XYA, MUI_XYAT), byte 2 of G, M
   (MUI_GOTO, ...). Padding 1: MUI_U8G2_V_PADDING of mui_u8g2.c.
   0: another command, the rows are not known */
static uint8_t menu_dirty_field(fds_t *fds)
{
  u8g2_t *u8g2 = (u8g2_t *)mui.graphics_data;
  uint8_t cmd, y, r0, r1;

  if ( fds == NULL )
    return 1;
  cmd = (uint8_t)(mui_get_fds_char(fds) & 0xdf);   /* upper and lower case, as mui */
  if ( cmd == 'F' || cmd == 'B' || cmd == 'A' || cmd == 'T' )
    y = mui_get_fds_char(fds + 4);
  else if ( cmd == 'G' || cmd == 'M' )
    y = mui_get_fds_char(fds + 2);
  else
    return 0;
  u8g2_SetFont(u8g2, font_noto_sc_24);          /* the font of the fields, style 0 */
  u8g2_GetBarRows(u8g2, (u8g2_uint_t)(y * MUI_U8G2_GRID), 1, &r0, &r1);
  if ( r0 > r1 )
    return 0;
  menu_dirty_rows(r0, r1);
  return 1;
}

void menu_event(uint8_t event)
{
  fds_t *focus = mui.cursor_focus_fds;
  uint16_t scroll_top = mui.form_scroll_top;

  if ( event == MENU_NEXT )
    mui_NextField(&mui);
  else if ( event == MENU_PREV )
    mui_PrevField(&mui);
  else
    mui_SendSelect(&mui);

  /* the rows of the field that had the focus and of the field that has it:
     a field changes only itself, or mui has asked for the whole screen
     (mui_UpdateWholeScreen()). A list that scrolls changes its other lines: the
     whole screen */
  if ( mui.form_scroll_top != scroll_top || !menu_dirty_field(focus) ||
      !menu_dirty_field(mui.cursor_focus_fds) )
    menu_update_whole_screen();
#ifdef MENU_FULL_UPDATES
  menu_update_whole_screen();            /* the reference: every key the whole screen */
#endif

  /* a new language, when the option is no longer being edited: as after a
     reboot, the menu starts again, in the other language; a new form, the
     whole screen */
  if ( !mui.is_mud && language != shown_language )
  {
    menu_fds();
    mui_GotoForm(&mui, 1, 0);
  }
}

/*=== to the display ===*/

void menu_draw(u8g2_t *u8g2)
{
  if ( !mui.update_whole_screen && dirty_low <= dirty_high )
    u8g2_FirstRows(u8g2, dirty_low, dirty_high);
  else
    u8g2_FirstPage(u8g2);
  do
  {
    menu_draw_status(u8g2);
    mui_Draw(&mui);
  } while ( u8g2_NextPage(u8g2) );
  mui.update_whole_screen = 0;        /* one-shot requests */
  menu_dirty_clear();
}
