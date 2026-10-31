/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  solder.c: the soldering station demo. A home screen and a settings menu
  (mui), in 24x24 Chinese, on the 240x280 display through clut_dma. The
  layout follows OpenT12, https://github.com/createskyblue/OpenT12, an open
  soldering station with a 128x64 OLED and 12-pixel Chinese; no code or
  images of it are used here.

  Text: gb24st, 24x24 (font/u8g2_font_gb24st_t_solder.c), full-width Latin
  at the ASCII codes; every character 3x3 tiles, drawn at x = 8 * i and
  baseline y = 8 * j + 22. Temperature: logisoso78.

  A u8g2 and mui application as any other: the u8g2 ST7789 driver, the
  picture loop of u8g2. The platform gives the u8x8 byte function, the SPI.
  The colors: u8x8_clut_stream_rgb444(), clut_dma in u8x8_clut_dma.c with
  USE_CLUT_DMA, else the software of u8g2.

  Memory: a u8g2 page buffer of PAGE_ROWS tile rows in fbmem (the SOLDER
  layout of testimg.h). Black and white: no tile map, a clut table of one
  entry, white on black (clut_dma NO_MAP). SOLDER_COLOR: a tile map and a
  clut of 16 pairs; the regions of the screen on the 8-pixel grid of the
  layout.

  The soldering iron is simulated, every SOLDER_TICK_MS: the heater warms the
  tip at up to 60 degrees per second, the tip loses 10% per second of its
  difference with the room. The control: the steady-state power for the set
  temperature, plus 2% per degree of difference.
*/

#include <string.h>
#include "u8g2.h"
#include "mui.h"
#include "mui_u8g2.h"
#include "testimg.h"

extern const uint8_t u8g2_font_gb24st_t_solder[];
#define FONT24 u8g2_font_gb24st_t_solder

#define T_ROOM   25                /* degrees */
#define T_MIN    200
#define T_MAX    450
#define T_STEP   5                 /* w s, and the menu list */
#define T_BAR    500               /* temperature at the end of the bar */
#define T_NEAR   10                /* within: holding */

#define BAR_Y    232               /* the temperature bar */

#ifdef SOLDER_COLOR
/* clut entries. C_STATE: the temperature, its unit and the temperature
   bar, in the color of the state. C_STATUS: the status, white, red on a
   sensor fault. two clut entries changed per frame, the map and the bitmap
   stay */
enum { C_BODY, C_TITLE, C_STATE, C_VALUE, C_POWER, C_STATUS };
#define RGB_WHITE  0xFFFFFFu
#define RGB_BLACK  0x000000u
#define RGB_TITLE  0x000000u       /* title band */
#define RGB_COLD   0x4090FFu       /* below the set temperature: heating */
#define RGB_READY  0x00E040u       /* within T_NEAR: ready */
#define RGB_HOT    0xFF3020u       /* above: too hot; and a sensor fault */
#define RGB_VALUE  0x00E0E0u       /* the set temperature */
#define RGB_POWER  0xFFB000u       /* heater power */
#endif

static u8g2_t u8g2;
static mui_t mui;
static uint8_t *base;              /* fbmem */

static int32_t tip16 = T_ROOM * 16; /* tip temperature, 1/16 degree */
static int16_t set_temp = 320;     /* set temperature, degrees */
static uint8_t power;              /* heater, 0..100 %, shown: power16 */
static int16_t power16;            /* power, 1/16 %, averaged over about 1 s */
static uint8_t fault;              /* simulated sensor fault */
static uint8_t tip;                /* the soldering tip, an entry of tips[] */
static uint8_t in_menu;

static const char *const tips[] = { "D24", "BC1", "K" };
#define TIPS (sizeof tips / sizeof tips[0])

/* ------------------------------------------------------------------ */
/* u8g2 */

/* gpio and delay: the platform initializes the display, not u8g2 */
static uint8_t u8x8_none(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
  return 1;
}

/* v in decimal at s; returns the end of the string */
static char *dec(char *s, int v)
{
  char t[6];
  int n = 0;

  if (v < 0) {
    *s++ = '-';
    v = -v;
  }
  do
    t[n++] = (char)('0' + v % 10);
  while ((v /= 10) != 0);
  while (n)
    *s++ = t[--n];
  *s = 0;
  return s;
}

/* ------------------------------------------------------------------ */
/* the menu */

static uint16_t m_set, m_tip;

static const char *m_set_name(void *data, uint16_t i)
{
  static char s[12];

  (void)data;
  strcpy(dec(s, T_MIN + T_STEP * i), "℃");
  return s;
}

static uint16_t m_set_cnt(void *data)
{
  (void)data;
  return (T_MAX - T_MIN) / T_STEP + 1;
}

static const char *m_tip_name(void *data, uint16_t i)
{
  (void)data;
  return tips[i];
}

static uint16_t m_tip_cnt(void *data)
{
  (void)data;
  return TIPS;
}

/* the fields of the menu framed when focused, not inverted: the 1-pixel
   strokes of gb24st read better light on dark. mui has this style (pf)
   only as a draw function; these pass every message but the drawing to
   the widgets of mui */
static uint8_t list_pf(mui_t *ui, uint8_t msg)
{
  if (msg == MUIF_MSG_DRAW) {
    mui_u8g2_list_t *l = (mui_u8g2_list_t *)muif_get_data(ui->uif);

    mui_u8g2_draw_button_pf(ui, ui->arg, 1, mui_u8g2_list_get_element_cb(l)(
        mui_u8g2_list_get_data_ptr(l), *mui_u8g2_list_get_selection_ptr(l)));
    return 0;
  }
  return mui_u8g2_u16_list_line_wa_mse_pi(ui, msg);
}

static uint8_t btn_exit_pf(mui_t *ui, uint8_t msg)
{
  if (msg == MUIF_MSG_DRAW) {
    mui_u8g2_draw_button_utf(ui, U8G2_BTN_HCENTER | (mui_IsCursorFocus(ui) ? 1u : 0u),
                             0, 1, 1, ui->text);   /* padding as mui_u8g2.c */
    return 0;
  }
  return mui_u8g2_btn_exit_wm_fi(ui, msg);
}

static muif_t m_list[] = {
  MUIF_U8G2_FONT_STYLE(0, FONT24),
  MUIF_RO("HR", mui_hline),
  MUIF_U8G2_LABEL(),
  MUIF_U8G2_U16_LIST("TS", &m_set, NULL, m_set_name, m_set_cnt, list_pf),
  MUIF_U8G2_U16_LIST("TT", &m_tip, NULL, m_tip_name, m_tip_cnt, list_pf),
  MUIF_BUTTON("BX", btn_exit_pf),
};

/* baselines 8 * j + 22: 22, then rows 54, 86, 118, 8 pixels apart, as
   the rows of the home screen.
   MUI_U8G2_HIRES scales y by 2 only
   on a display of more than 255 rows: there y / 2. the title centered,
   clear of the rounded corners */
static fds_t m_fds[] =
  MUI_FORM(1)
  MUI_STYLE(0)
  MUI_LABEL(96, 11, "设置")
  MUI_XY("HR", 0, 14)
  MUI_LABEL(0, 27, "温度")
  MUI_XYA("TS", 96, 27, 136)
  MUI_LABEL(0, 43, "烙铁头")
  MUI_XYA("TT", 96, 43, 136)
  MUI_XYT("BX", 120, 59, "返回")
  ;

/* ------------------------------------------------------------------ */
/* color: the tile map and the clut */

#ifdef SOLDER_COLOR
#define TCOLS (LCD_W / 8u)

/* the regions of the home screen, in tiles: every region on the grid of
   the layout. the set temperature after "目标 " in its own color: per
   character, 3 tiles each */
static void colors_home(u8g2_t *u)
{
  u8g2_SetClutRegion(u, 0, 0, TCOLS, ROWS, C_BODY);
  u8g2_SetClutRegion(u, 0, 0, TCOLS, 3, C_TITLE);          /* title, rows 0..23 */
  u8g2_SetClutRegion(u, 0, 4, 23, 10, C_STATE);            /* temperature and unit, rows 32..111 */
  u8g2_SetClutRegion(u, 0, 15, 6, 3, C_STATUS);            /* status, 2 characters */
  u8g2_SetClutRegion(u, 9, 19, 12, 3, C_VALUE);            /* "320℃", 4 characters */
  u8g2_SetClutRegion(u, 9, 23, 21, 3, C_POWER);            /* "100%" and the bar */
  u8g2_SetClutRegion(u, 2, BAR_Y / 8, 26, 3, C_STATE);     /* temperature bar */
}

static void colors_menu(u8g2_t *u)
{
  u8g2_SetClutRegion(u, 0, 0, TCOLS, ROWS, C_BODY);
  u8g2_SetClutRegion(u, 0, 0, TCOLS, 3, C_TITLE);
}

static void colors_init(u8g2_t *u)
{
  _Static_assert((CLUT_OFS & 63u) == 0, "clut_dma: clut 64-byte aligned");
  memset(base + MAP_OFS, 0, ROWS * TCOLS / 2u);
  u8g2_SetClutMap(u, base + MAP_OFS);
  u8g2_SetClutTable(u, (const uint16_t (*)[2])(void *)(base + CLUT_OFS));
  u8g2_SetClutColor(u, C_BODY, RGB_WHITE, RGB_BLACK);
  u8g2_SetClutColor(u, C_TITLE, RGB_WHITE, RGB_TITLE);
  u8g2_SetClutColor(u, C_STATE, RGB_COLD, RGB_BLACK);
  u8g2_SetClutColor(u, C_VALUE, RGB_VALUE, RGB_BLACK);
  u8g2_SetClutColor(u, C_POWER, RGB_POWER, RGB_BLACK);
  u8g2_SetClutColor(u, C_STATUS, RGB_WHITE, RGB_BLACK);
  colors_home(u);
}
#endif

/* ------------------------------------------------------------------ */
/* the home screen */

static void draw_home(u8g2_t *u)
{
  char s[24], *e;
  int t = (int)((tip16 + 8) / 16);
  const char *status;
  int x;

  u8g2_SetFontMode(u, 1);
  u8g2_SetFont(u, FONT24);

  /* title: "烙铁头" and the tip, centered on the grid, clear of the rounded
     corners of the display; a line under it, as the title of the menu. not
     inverted: the 1-pixel strokes of gb24st read better light on dark */
  u8g2_SetDrawColor(u, 1);
  strcpy(s, "烙铁头 ");
  strcat(s, tips[tip]);
  u8g2_DrawUTF8(u, (u8g2_uint_t)(((LCD_W - u8g2_GetUTF8Width(u, s)) / 2) & ~7u), 22, s);
  u8g2_DrawHLine(u, 0, 28, LCD_W);

  /* the temperature: 3 digits of 50 pixels, right-aligned at x 150, rows
     32..109; the unit after it */
  u8g2_SetFont(u, u8g2_font_logisoso78_tn);
  if (fault)
    strcpy(s, "---");
  else
    dec(s, t);
  x = 150 - 50 * (int)strlen(s);
  u8g2_DrawStr(u, x < 0 ? 0 : x, 110, s);
  u8g2_SetFont(u, FONT24);
  u8g2_DrawUTF8(u, 160, 54, "℃");

  /* status */
  if (fault)
    status = "故障";
  else if (t < set_temp - T_NEAR)
    status = "加热";
  else if (t > set_temp + T_NEAR)
    status = "冷却";
  else
    status = "恒温";
#ifdef SOLDER_COLOR
  u8g2_SetClutColor(u, C_STATE, fault || t > set_temp + T_NEAR ? RGB_HOT
                                : t < set_temp - T_NEAR ? RGB_COLD : RGB_READY, RGB_BLACK);
  u8g2_SetClutColor(u, C_STATUS, fault ? RGB_HOT : RGB_WHITE, RGB_BLACK);
#endif
  u8g2_DrawUTF8(u, 0, 142, status);

  /* set temperature */
  strcpy(s, "目标 ");
  e = dec(s + strlen(s), set_temp);
  strcpy(e, "℃");
  u8g2_DrawUTF8(u, 0, 174, s);

  /* heater power, and a bar of 8 tiles after "功率 100%" (7 characters) */
  strcpy(s, "功率 ");
  e = dec(s + strlen(s), (power16 + 8) / 16);
  strcpy(e, "%");
  u8g2_DrawUTF8(u, 0, 206, s);
  u8g2_DrawFrame(u, 176, 184, 64, 24);
  u8g2_DrawBox(u, 178, 186, (u8g2_uint_t)(60 * ((power16 + 8) / 16) / 100), 20);

  /* temperature bar, 0..T_BAR degrees, the set temperature a mark. x 16..223,
     rows BAR_Y..BAR_Y + 23: clear of the rounded corners of the display */
  u8g2_DrawFrame(u, 16, BAR_Y, 208, 24);
  if (!fault && t > 0)
    u8g2_DrawBox(u, 18, BAR_Y + 2, (u8g2_uint_t)(204 * (t < T_BAR ? t : T_BAR) / T_BAR), 20);
  u8g2_SetDrawColor(u, 2);
  u8g2_DrawBox(u, (u8g2_uint_t)(18 + 204 * set_temp / T_BAR - 1), BAR_Y - 4, 3, 32);
  u8g2_SetDrawColor(u, 1);
  u8g2_SetFontMode(u, 0);
}

/* the picture loop of u8g2: the driver sends each page, a tile row at a
   time */
static void show(void)
{
  u8g2_FirstPage(&u8g2);
  do
    if (in_menu)
      mui_Draw(&mui);
    else
      draw_home(&u8g2);
  while (u8g2_NextPage(&u8g2));
}

/* ------------------------------------------------------------------ */

static void model_step(void)
{
  int t = (int)(tip16 / 16);
  int p = (set_temp - T_ROOM) * 100 / 600 + 2 * (set_temp - t);

  if (fault || p < 0)
    p = 0;
  if (p > 100)
    p = 100;
  power = (uint8_t)p;
  /* the shown power: an exponential average over 8 steps. at the set
     temperature the control alternates between two values */
  power16 += (int16_t)((p * 16 - power16) / 8);
  /* per tick of 0.1 s: heating 6 degrees at 100%, loss 1% of the
     difference with the room */
  tip16 += p * 96 / 100 - (tip16 - T_ROOM * 16) / 100;
}

void solder_init(volatile uint8_t *mem, demo_byte byte)
{
  uint8_t *m = (uint8_t *)(uintptr_t)mem;

  base = m;
  u8g2_SetupDisplay(&u8g2, u8x8_d_st7789_240x280, u8x8_cad_011, (u8x8_msg_cb)byte, u8x8_none);
  u8g2_SetupBuffer(&u8g2, m + FB_OFS, PAGE_ROWS, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_SetFontPosBaseline(&u8g2);
#ifdef SOLDER_COLOR
  colors_init(&u8g2);
#else
  /* no tile map: entry 0 of the table for every tile, one word, white on
     black. in fbmem, where clut_dma reads it */
  *(volatile uint32_t *)(mem + CLUT_OFS) = 0x00000FFFu;
  u8g2_SetClutTable(&u8g2, (const uint16_t (*)[2])(void *)(m + CLUT_OFS));
#endif
  mui_Init(&mui, &u8g2, m_fds, m_list, sizeof m_list / sizeof m_list[0]);
  show();
}

void solder_tick(unsigned steps)
{
  while (steps--)
    model_step();
  if (!in_menu)
    show();
}

/* the screen shown, home or menu, drawn and sent again: the benchmark */
void solder_frame(void)
{
  show();
}

int solder_key(char key)
{
  if (!in_menu) {
    switch (key) {
    case 'w':
      if (set_temp < T_MAX)
        set_temp += T_STEP;
      break;
    case 's':
      if (set_temp > T_MIN)
        set_temp -= T_STEP;
      break;
    case 'e':
      fault = !fault;
      break;
    case ' ':
    case '\r':
    case '\n':
      m_set = (uint16_t)((set_temp - T_MIN) / T_STEP);
      m_tip = tip;
      mui_GotoForm(&mui, 1, 0);
      in_menu = 1;
#ifdef SOLDER_COLOR
      colors_menu(&u8g2);
#endif
      break;
    default:
      return 0;
    }
  } else {
    switch (key) {
    case 'w':  mui_PrevField(&mui); break;
    case 's':  mui_NextField(&mui); break;
    case 'a':  mui_SendValueDecrement(&mui); break;
    case 'd':  mui_SendValueIncrement(&mui); break;
    case ' ':
    case '\r':
    case '\n': mui_SendSelect(&mui); break;
    default:   return 0;
    }
    if (!mui_IsFormActive(&mui)) {   /* 返回: back to the home screen */
      set_temp = (int16_t)(T_MIN + T_STEP * m_set);
      tip = (uint8_t)m_tip;
      in_menu = 0;
#ifdef SOLDER_COLOR
      colors_home(&u8g2);
#endif
    }
  }
  show();
  return 1;
}
