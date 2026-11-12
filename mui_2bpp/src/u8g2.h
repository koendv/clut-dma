/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8g2.h: a minimal u8g2 for a frame buffer of 2 bits per pixel, enough to
  run mui (mui.c, mui_u8g2.c) with anti-aliased fonts.

  A menu is drawn with three things:
    glyphs   the text and the controls (checkbox, radio button, the mark of
             the chosen value) come from fonts, anti-aliased
    tiles    the color of each 8x8 tile is a clut entry in the tile map: the
             rows of the screen have roles, and the focus, a value being
             edited and a button are a color of their tiles, not a drawing
    clut     a foreground, a background and two blends per role, computed
             by the compiler, gamma 2.2, and set once, at the start
  Boxes and frames are drawn only for the bar fields of mui.

  Frame buffer, as read by clut_dma with CTRL BPP2:
    row layout, a pixel row is U8G2_LINE_STRIDE bytes, 4 pixels to a byte,
    the leftmost in bits 7:6. Pixel codes: 0 background, 1 a third, 2 two
    thirds, 3 foreground. A glyph pixel takes the larger of its level and
    the one below; a box sets its pixels to 3.

  Page buffer: the buffer holds U8G2_PAGE_ROWS tile rows (8 pixel rows each).
  u8g2_FirstPage()/u8g2_NextPage() draw the screen a page at a time; with
  U8G2_PAGE_ROWS = U8G2_TILE_ROWS, the default, one page is the screen.
  u8g2_FirstRows(first, last) instead of u8g2_FirstPage(): only tile rows
  first .. last are cleared, drawn and sent, a page at a time from row
  first. With the full buffer the rows are drawn in place: the buffer stays
  the screen. Everything is drawn in every pass, clipped to its rows.

  Display: the u8x8 layer of u8g2, unchanged (../u8x8), with its display
  drivers, e.g. u8x8_d_st7789_240x280. The screen of the library may be
  smaller than the panel: u8g2_ClearDisplay() clears all of the panel.
  No rotation in software: u8g2_SetFlipMode() rotates by 180 degrees.

  mui: with the changes of mui_2bpp in mui_u8g2.c (MUI_U8G2_2BPP), all
  styles of fields are drawn as the pi style: the focus is the focus color
  (U8G2_ROLE_INVERTED, the name from the inverted bar of u8g2), a value
  being edited the edit color; the buttons of the fi style have the button
  color.
*/

#ifndef U8G2_H
#define U8G2_H

#include "u8x8.h"

#define MUI_U8G2_2BPP           /* mui_u8g2.c: the styles and controls of mui_2bpp */

/* the screen, in pixels: a multiple of 8 */
#ifndef U8G2_DISPLAY_WIDTH
#define U8G2_DISPLAY_WIDTH 240
#endif
#ifndef U8G2_DISPLAY_HEIGHT
#define U8G2_DISPLAY_HEIGHT 240
#endif

#define U8G2_TILE_COLS (U8G2_DISPLAY_WIDTH / 8)
#define U8G2_TILE_ROWS (U8G2_DISPLAY_HEIGHT / 8)
#define U8G2_LINE_STRIDE (2 * U8G2_TILE_COLS)    /* bytes per pixel row */

/* tile rows in the buffer: 1 .. U8G2_TILE_ROWS */
#ifndef U8G2_PAGE_ROWS
#define U8G2_PAGE_ROWS U8G2_TILE_ROWS
#endif

/* bytes of the buffer; 2-byte aligned (clut_dma: 4-byte) */
#define U8G2_BUF_SIZE (U8G2_PAGE_ROWS * 8 * U8G2_LINE_STRIDE)

/* bytes of the tile map: a nibble per tile */
#define U8G2_MAP_SIZE ((U8G2_TILE_COLS * U8G2_TILE_ROWS + 1) / 2)

/*
  Roles: the screen is in rows of roles, in tile rows from the top: the
  status row, the title, the content, Back at the bottom. Each role has its
  clut entry, set with u8g2_SetRoleColor() or u8g2_SetRoleClut(). Where mui draws a bar
  (u8g2_DrawButtonFrame()) its tiles take a role: U8G2_BTN_INV the inverted
  role (the field with the focus), with U8G2_BTN_XFRAME the edit role (the
  field being edited), a border the button role.
*/
#ifndef U8G2_STATUS_ROWS
#define U8G2_STATUS_ROWS 2
#endif
#ifndef U8G2_TITLE_ROWS
#define U8G2_TITLE_ROWS 4
#endif
#ifndef U8G2_BACK_ROWS
#define U8G2_BACK_ROWS 4
#endif

#define U8G2_ROLE_CONTENT 0     /* entry 0: also without a tile map */
#define U8G2_ROLE_TITLE 1
#define U8G2_ROLE_BACK 2
#define U8G2_ROLE_STATUS 3
#define U8G2_ROLE_INVERTED 4
#define U8G2_ROLE_EDIT 5
#define U8G2_ROLE_BUTTON 6
#define U8G2_ROLE_SLIDER 7      /* the bar of a bar field: 4 colors of their own */
/* the rounded corners of the bars: the bar color on the screen color */
#define U8G2_ROLE_INVERTED_CORNER 8
#define U8G2_ROLE_EDIT_CORNER 9
#define U8G2_ROLE_BUTTON_CORNER 10
#define U8G2_ROLE_CNT 11

/* the glyphs of the controls in the control font (Material Symbols) */
#ifndef U8G2_CONTROL_CHECKBOX_OFF
#define U8G2_CONTROL_CHECKBOX_OFF 0xE835  /* check_box_outline_blank */
#define U8G2_CONTROL_CHECKBOX_ON 0xE9DE   /* check_box */
#define U8G2_CONTROL_RADIO_OFF 0xE836     /* radio_button_unchecked */
#define U8G2_CONTROL_RADIO_ON 0xE837      /* radio_button_checked */
#define U8G2_CONTROL_MARK 0xE837          /* the chosen value of a list */
#endif

#define U8G2_16BIT
typedef uint16_t u8g2_uint_t;
typedef int16_t u8g2_int_t;
typedef int32_t u8g2_long_t;

typedef struct u8g2_struct u8g2_t;
typedef u8g2_uint_t (*u8g2_font_calc_vref_fnptr)(u8g2_t *u8g2);

struct u8g2_struct
{
  u8x8_t u8x8;                  /* first: u8g2_GetU8x8() */
  uint8_t *buf;                 /* U8G2_BUF_SIZE bytes */
  uint8_t page_row;             /* the first tile row of the pass */
  uint8_t last_row;             /* the last tile row of the update */
  uint8_t buf_row;              /* the tile row at the start of the buffer */
  const uint8_t *font;
  const uint8_t *control_font;
  int8_t font_ref_ascent;       /* pixels above the baseline */
  int8_t font_ref_descent;      /* below the baseline: negative */
  u8g2_font_calc_vref_fnptr font_calc_vref;   /* 0: y is the baseline */
  uint8_t *map;                 /* roles: the tile map, NULL without roles */
  uint32_t (*clut)[2];          /* roles: the clut */
};

#define u8g2_GetU8x8(u8g2) ((u8x8_t *)(u8g2))
#define u8g2_GetDisplayWidth(u8g2) ((u8g2_uint_t)U8G2_DISPLAY_WIDTH)
#define u8g2_GetDisplayHeight(u8g2) ((u8g2_uint_t)U8G2_DISPLAY_HEIGHT)
#define u8g2_GetBufferPtr(u8g2) ((u8g2)->buf)
#define u8g2_GetAscent(u8g2) ((u8g2)->font_ref_ascent)
#define u8g2_GetDescent(u8g2) ((u8g2)->font_ref_descent)
#define u8g2_GetMaxCharWidth(u8g2) ((u8g2)->font[4])
#define u8g2_SetFontMode(u8g2, is_transparent) ((void)(u8g2), (void)(is_transparent))

#define u8g2_InitDisplay(u8g2) u8x8_InitDisplay(u8g2_GetU8x8(u8g2))
#define u8g2_SetPowerSave(u8g2, is_enable) u8x8_SetPowerSave(u8g2_GetU8x8(u8g2), (is_enable))
#define u8g2_SetFlipMode(u8g2, mode) u8x8_SetFlipMode(u8g2_GetU8x8(u8g2), (mode))

/* low level, without roles: a tile map (4-bit clut entries, the even tile
   in the low nibble; NULL: entry 0 everywhere) and a clut of the
   application, passed on to the stream function */
#define u8g2_SetClutMap(u8g2, map) (u8g2_GetU8x8(u8g2)->tile_clut_map = (map))

/* the clut: 16 entries of two 32-bit words, as read by clut_dma: the color
   of code c in entry i is word clut[i][c >> 1], bits 15:0 when c is odd,
   31:16 when even. 128-byte aligned for clut_dma. NULL: a gray ramp */
void u8g2_SetClutTable2bpp(u8g2_t *u8g2, const uint32_t (*clut)[2]);
const uint32_t (*u8g2_GetClutTable2bpp(u8x8_t *u8x8))[2];

/* buttons: flags of u8g2_DrawButtonUTF8() and u8g2_DrawButtonFrame() */
#define U8G2_BTN_BW_POS 0
#define U8G2_BTN_BW_MASK 7
#define U8G2_BTN_BW0 0x00
#define U8G2_BTN_BW1 0x01
#define U8G2_BTN_BW2 0x02
#define U8G2_BTN_BW3 0x03
#define U8G2_BTN_SHADOW_POS 3
#define U8G2_BTN_SHADOW_MASK 0x18
#define U8G2_BTN_SHADOW0 0x08
#define U8G2_BTN_SHADOW1 0x10
#define U8G2_BTN_SHADOW2 0x18
#define U8G2_BTN_INV 0x20
#define U8G2_BTN_HCENTER 0x40
#define U8G2_BTN_XFRAME 0x80

#ifdef __cplusplus
extern "C" {
#endif

/* u8g2_2bpp.c */
void u8g2_SetupDisplay2bpp(u8g2_t *u8g2, uint8_t *buf, u8x8_msg_cb display_cb, u8x8_msg_cb cad_cb,
    u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb);
void u8g2_Setup_st7789_240x280(u8g2_t *u8g2, uint8_t *buf, u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb);

/* roles: map, U8G2_MAP_SIZE bytes, and clut, 16 entries, 128-byte aligned:
   memory the stream function (or clut_dma) reads */
void u8g2_SetupClut2bpp(u8g2_t *u8g2, uint8_t *map, uint32_t (*clut)[2]);
/* the tiles of a rectangle, rounded to the nearest tile edges, take the
   clut entry of role */
void u8g2_MarkTiles(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, uint8_t role);
/* the bar of a field, as u8g2_MarkTiles(), with rounded corners for the
   focus, edit and button roles: the bar one tile wider at each end (the
   forms keep those tiles free), its four corner tiles in the corner role
   (role + 4), a quarter disc of radius 7 drawn in each */
void u8g2_MarkBar(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, uint8_t role);
/* the tile rows u8g2_MarkTiles() marks for pixel rows y .. y+h-1; none: *r0 > *r1 */
void u8g2_GetTileRows(u8g2_t *u8g2, u8g2_uint_t y, u8g2_uint_t h, uint8_t *r0, uint8_t *r1);

void u8g2_ClearBuffer(u8g2_t *u8g2);
void u8g2_FirstPage(u8g2_t *u8g2);
/* as u8g2_FirstPage(), for tile rows first .. last only */
void u8g2_FirstRows(u8g2_t *u8g2, uint8_t first, uint8_t last);
uint8_t u8g2_NextPage(u8g2_t *u8g2);
void u8g2_ClearDisplay(u8g2_t *u8g2);

/* a pixel of level 0 .. 3 (the code: 0 background, 3 foreground), clipped;
   it keeps the larger level. Not in u8g2, which has draw colors */
void u8g2_DrawPixelLevel(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, uint8_t level);

/* u8g2_line.c: as u8g2_DrawLine() of u8g2, anti-aliased (Xiaolin Wu,
   integers) */
void u8g2_DrawLine(u8g2_t *u8g2, u8g2_uint_t x1, u8g2_uint_t y1, u8g2_uint_t x2, u8g2_uint_t y2);

/* u8g2_circle.c: as u8g2_DrawCircle() and u8g2_DrawDisc() of u8g2,
   anti-aliased (coverage, integers); option: the quarters */
#define U8G2_DRAW_UPPER_RIGHT 0x01
#define U8G2_DRAW_UPPER_LEFT  0x02
#define U8G2_DRAW_LOWER_LEFT 0x04
#define U8G2_DRAW_LOWER_RIGHT  0x08
#define U8G2_DRAW_ALL (U8G2_DRAW_UPPER_RIGHT|U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_LOWER_RIGHT|U8G2_DRAW_LOWER_LEFT)
void u8g2_DrawCircle(u8g2_t *u8g2, u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t option);
void u8g2_DrawDisc(u8g2_t *u8g2, u8g2_uint_t x0, u8g2_uint_t y0, u8g2_uint_t rad, uint8_t option);

/* for the bar fields of mui: the pixels of a box or a frame set to 3 */
void u8g2_DrawBox(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h);
void u8g2_DrawFrame(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h);
/* mui_hline(), a rule across the screen; the roles of the rows separate
   the parts of a menu without it */
#define u8g2_DrawHLine(u8g2, x, y, w) u8g2_DrawBox((u8g2), (x), (y), (w), 1)

void u8g2_SetFont(u8g2_t *u8g2, const uint8_t *font);
u8g2_uint_t u8g2_DrawStr(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str);
u8g2_uint_t u8g2_DrawUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str);
u8g2_uint_t u8g2_GetStrWidth(u8g2_t *u8g2, const char *str);
u8g2_uint_t u8g2_GetUTF8Width(u8g2_t *u8g2, const char *str);

/* controls: the glyph cp of the control font, centred in the box of w x w
   pixels from x, above the baseline y (the box of a checkbox of mui) */
void u8g2_SetControlFont(u8g2_t *u8g2, const uint8_t *font);
void u8g2_DrawControl(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, uint16_t cp);

/* u8g2_button.c */
void u8g2_DrawButtonFrame(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t flags,
    u8g2_uint_t text_width, u8g2_uint_t padding_h, u8g2_uint_t padding_v);
void u8g2_DrawButtonUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t flags,
    u8g2_uint_t width, u8g2_uint_t padding_h, u8g2_uint_t padding_v, const char *text);
/* the tile rows of the bar of a field with baseline y, in the current font,
   as u8g2_DrawButtonFrame() marks them; none: *r0 > *r1 */
void u8g2_GetBarRows(u8g2_t *u8g2, u8g2_uint_t y, u8g2_uint_t padding_v, uint8_t *r0, uint8_t *r1);

#ifdef __cplusplus
}
#endif

/*
  The colors of a role: u8g2_SetRoleColor(u8g2, role, fg, bg), fg and bg
  rgb888 constants. Code 0 is bg, 3 fg, 1 and 2 blends at a third and two
  thirds, mixed in linear light: c^gamma per channel, mixed, back with the
  power 1/gamma. The gamma depends on the role: U8G2_GAMMA (2.2) for light
  text on a darker background, U8G2_GAMMA_DARK for dark text on a lighter
  background, which in linear light would look thin; the lighter of fg and
  bg by luminance (Rec. 709 weights on the 8-bit values). The compiler computes the colors (GCC: math
  builtins in a static initializer), in the native format of the display,
  U8G2_CLUT_NATIVE: 444 (rgb444, the ST7789 driver of u8x8) or 565. A
  color that is not a constant is a compile error. Setting a role is two
  stores of constant words.

  u8g2_SetRoleClut(u8g2, role, c0, c1, c2, c3): the four colors of a role
  directly, in the native format.

  A compiler that does not accept u8g2_SetRoleColor() (it is GCC only;
  clang refuses the math builtins in a static initializer): compute the
  four colors with tools/clutline.py, the same formula, and set them with
  u8g2_SetRoleClut():
    clutline.py E8E8E8 101828
    0x112, 0x888, 0xbbb, 0xeee   ...
    u8g2_SetRoleClut(u8g2, U8G2_ROLE_CONTENT, 0x112, 0x888, 0xbbb, 0xeee);
*/
#ifndef U8G2_GAMMA
#define U8G2_GAMMA 2.2           /* light text on a darker background */
#endif
#ifndef U8G2_GAMMA_DARK
#define U8G2_GAMMA_DARK 1.0      /* dark text on a lighter background */
#endif
#ifndef U8G2_CLUT_NATIVE
#define U8G2_CLUT_NATIVE 444
#endif

/* the gamma of a role: fg lighter than bg, or not */
#define U8G2_Y_(rgb) (2126L * (((rgb) >> 16) & 0xFF) + 7152L * (((rgb) >> 8) & 0xFF) + 722L * ((rgb) & 0xFF))
#define U8G2_GAMMA_OF_(f, b) (U8G2_Y_(f) >= U8G2_Y_(b) ? U8G2_GAMMA : U8G2_GAMMA_DARK)
/* channel s of rgb888, in linear light with gamma g, 0..1 */
#define U8G2_LIN_(rgb, s, g) __builtin_pow((((rgb) >> (s)) & 0xFF) / 255.0, (g))
/* k thirds of the way from b to f, back to 0..1 */
#define U8G2_MIX_(f, b, k, s) \
  __builtin_pow((U8G2_LIN_(f, s, U8G2_GAMMA_OF_(f, b)) * (k) + \
                 U8G2_LIN_(b, s, U8G2_GAMMA_OF_(f, b)) * (3 - (k))) / 3.0, 1 / U8G2_GAMMA_OF_(f, b))
#define U8G2_Q_(x, m) ((uint32_t)((x) * (m) + 0.5))
#if U8G2_CLUT_NATIVE == 565
#define U8G2_NATIVE_(f, b, k) (U8G2_Q_(U8G2_MIX_(f, b, k, 16), 31) << 11 | \
  U8G2_Q_(U8G2_MIX_(f, b, k, 8), 63) << 5 | U8G2_Q_(U8G2_MIX_(f, b, k, 0), 31))
#else
#define U8G2_NATIVE_(f, b, k) (U8G2_Q_(U8G2_MIX_(f, b, k, 16), 15) << 8 | \
  U8G2_Q_(U8G2_MIX_(f, b, k, 8), 15) << 4 | U8G2_Q_(U8G2_MIX_(f, b, k, 0), 15))
#endif

/* the two words of a clut entry, as clut_dma reads them: code c in word
   c >> 1, bits 15:0 when c is odd, 31:16 when even */
#define U8G2_ROLE_WORDS(fg, bg) { \
  U8G2_NATIVE_(fg, bg, 0) << 16 | U8G2_NATIVE_(fg, bg, 1), \
  U8G2_NATIVE_(fg, bg, 2) << 16 | U8G2_NATIVE_(fg, bg, 3) }

#if defined(__GNUC__) && !defined(__clang__)
#define u8g2_SetRoleColor(u8g2, role, fg, bg) do { \
    static const uint32_t u8g2_rc_[2] = U8G2_ROLE_WORDS(fg, bg); \
    (u8g2)->clut[role][0] = u8g2_rc_[0]; \
    (u8g2)->clut[role][1] = u8g2_rc_[1]; \
  } while (0)
/* a role of 4 colors of its own, not blends: code c is color c, rgb888
   constants (U8G2_ROLE_SLIDER: the screen, the track, unused, the
   indicator) */
#define U8G2_COLOR_NATIVE_(c) U8G2_NATIVE_(c, c, 3)
#define u8g2_SetRolePalette(u8g2, role, c0, c1, c2, c3) do { \
    static const uint32_t u8g2_rp_[2] = { \
      U8G2_COLOR_NATIVE_(c0) << 16 | U8G2_COLOR_NATIVE_(c1), \
      U8G2_COLOR_NATIVE_(c2) << 16 | U8G2_COLOR_NATIVE_(c3) }; \
    (u8g2)->clut[role][0] = u8g2_rp_[0]; \
    (u8g2)->clut[role][1] = u8g2_rp_[1]; \
  } while (0)
#else
#define u8g2_SetRoleColor(u8g2, role, fg, bg) _Static_assert(0, \
    "u8g2_SetRoleColor() needs GCC: use tools/clutline.py and u8g2_SetRoleClut()")
#define u8g2_SetRolePalette(u8g2, role, c0, c1, c2, c3) _Static_assert(0, \
    "u8g2_SetRolePalette() needs GCC: use u8g2_SetRoleClut()")
#endif

static inline void u8g2_SetRoleClut(u8g2_t *u8g2, uint8_t role,
    uint16_t c0, uint16_t c1, uint16_t c2, uint16_t c3)
{
  u8g2->clut[role][0] = (uint32_t)c0 << 16 | c1;
  u8g2->clut[role][1] = (uint32_t)c2 << 16 | c3;
}

#endif /* U8G2_H */
