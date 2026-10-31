/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  testimg.h: test images and request sequences, shared by the firmware
  (main.c) and the simulation (host.c, fpga/tb/tb_frame.v): both send the
  same bytes to the display.
*/

#ifndef TESTIMG_H
#define TESTIMG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* display: 240x280 window at row 20 of the 240x320 ram, as in u8g2 */
#define LCD_W      240u
#define LCD_H      280u
#define LCD_YOFS   20u
#ifndef LCD_MADCTL
#define LCD_MADCTL 0x00u          /* connector at the top; 0xC0 (MY, MX): at the bottom */
#endif

#define COLS (LCD_W / 8)           /* TILE_COLS, bitmap bytes per pixel row */
#define ROWS (LCD_H / 8)

/* fbmem layout, offsets from the start of fbmem */
#ifdef SOLDER_TEST
/* the soldering station demo, solder.c: only the memory it uses, a page
   buffer of PAGE_ROWS tile rows. FBMEM_USED: the bytes in use.
   black and white: the clut word of NO_MAP, a table of one entry, then the
   page buffer (8-byte aligned). SOLDER_COLOR: the clut (16 pairs, 64-byte
   aligned), the tile map, then the page buffer */
#ifndef PAGE_ROWS
#define PAGE_ROWS 2
#endif
#ifdef SOLDER_COLOR
#define CLUT_OFS  0x0000u          /* 16 pairs, rgb444 */
#define MAP_OFS   0x0040u          /* ROWS * COLS nibbles */
#define FB_OFS    ((MAP_OFS + ROWS * COLS / 2u + 7u) & ~7u)
#else
#define CLUT_OFS  0x0000u          /* NO_MAP: one word, white on black */
#define MAP_OFS   0x0000u          /* not used: NO_MAP */
#define FB_OFS    0x0008u          /* PAGE_ROWS * 240 bytes */
#endif
#define MONO_OFS  CLUT_OFS
#define FBMEM_USED (FB_OFS + PAGE_ROWS * 8u * COLS)
#else
#define FB_OFS    0x0000u          /* 30 * 280 = 8400 bytes */
#define MAP_OFS   0x2100u          /* 30 * 35 nibbles = 525 bytes */
#define CLUT_OFS  0x2340u          /* rgb565 */
#define MONO_OFS  0x2440u          /* NO_MAP: one word, white on black, any format */
#endif
#define CLUT2_OFS 0x2380u          /* rgb565, fg and bg swapped */
#define CLUT4_OFS 0x23C0u          /* rgb444 */
#define CLUT3_OFS 0x2400u          /* rgb565, border test */
#define MONO2_OFS 0x2480u          /* NO_MAP: one word, black on white, any format */

/* clut_dma FORMAT */
#define FMT_RGB565 0u
#define FMT_RGB444 1u
#define FMT_RGB666 2u
/* clut_dma CTRL VERT, here in bit 2 of the format: the bitmap in the
   vertical tile layout of u8g2, FB_START the address of the first tile */
#define FMT_VERT   4u
/* clut_dma CTRL NO_MAP, here in bit 3 of the format: no tile map, every
   tile clut entry 0 */
#define FMT_NOMAP  8u

/* a step of a test. REQ: a clut_dma request of w x h pixels at tile (tx, ty)
   in format fmt, with the clut at offset clut. COLMOD: the display color
   format fmt. DELAY: w ms, only on the hardware */
enum { STEP_END, STEP_REQ, STEP_COLMOD, STEP_DELAY };

struct step {
  uint8_t     kind, fmt, tx, ty;
  uint16_t    w, h, clut;
  const char *name;
};

void pattern(volatile uint8_t *mem);
void border(volatile uint8_t *mem);

/* the u8g2 demos (ClutPalette, ClutColor, ClutClock, demo_graphics,
   demo_mui, the soldering station) send through u8g2: the u8g2 st7789
   driver, a tile row at a time, and the u8x8 byte function
   of the platform, the spi with DC and chip select. the pixels:
   u8x8_clut_stream_rgb444(), with clut_dma from u8x8_clut_dma.c
   (USE_CLUT_DMA), else the cpu (u8g2) */
struct u8x8_struct;
typedef uint8_t (*demo_byte)(struct u8x8_struct *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

void palette(volatile uint8_t *mem, demo_byte byte);

/* tile rows of the u8g2 buffer of ClutClock, demo_graphics and demo_mui: ROWS for a full
   buffer, 1 or 2 for the page buffer modes of u8g2 */
#ifndef PAGE_ROWS
#define PAGE_ROWS ROWS
#endif

void clutcolor_init(volatile uint8_t *mem, demo_byte byte);
void clutcolor_tick(void);         /* every second */
void clutclock_init(volatile uint8_t *mem, demo_byte byte);
void clutclock_tick(void);         /* every second */
void graphics_init(volatile uint8_t *mem, demo_byte byte);
void graphics_page(uint8_t page, uint8_t a);
uint16_t graphics_step(void);      /* returns the pause in ms */
#define GRAPHICS_PAGES 11
void mui_demo_init(volatile uint8_t *mem, demo_byte byte);
void mui_demo_key(char key);       /* w s a d, space */

extern const struct step pattern_steps[], border_steps[];

/* the Adafruit GFX demo, stars.cpp: the bitmap in the horizontal layout of
   GFXcanvas1, a pixel row of COLS bytes. an update is a rectangle of w x h
   pixels, top left at tile (tx, ty), in rgb444 with the clut at CLUT_OFS */
typedef void (*row_send)(uint8_t tx, uint8_t ty, uint16_t w, uint16_t h, const char *what);

void stars_init(volatile uint8_t *mem, row_send send);
void stars_tick(volatile uint8_t *mem, row_send send, int full);   /* a frame */

/* the soldering station demo, solder.c: a home screen and a menu in 24x24
   Chinese, a u8g2 and mui application, sent as the demos above.
   solder_tick: steps of the simulated soldering iron, SOLDER_TICK_MS each
   (several if the last frame took longer); it draws and sends the home
   screen. solder_key: w s (up down), space enter, e (sensor fault);
   returns 1 if it drew the screen */
#define SOLDER_TICK_MS 100u
void solder_init(volatile uint8_t *mem, demo_byte byte);
void solder_tick(unsigned steps);
int  solder_key(char key);
void solder_frame(void);           /* the screen shown, again */

/* the auto gauge, async.c: the whole screen in one clut_dma request,
   interrupt driven. async_frame: sleeps until the last frame is sent,
   updates the changes, starts the next send, returns while it is sent.
   async_stats: FPS and CPU %, smoothed, in tenths */
#ifndef ASYNC_MADCTL
#define ASYNC_MADCTL 0x60u         /* landscape 280x240: MX, MV; 0xA0 (MY, MV) the other way round */
#endif
void async_init(volatile uint8_t *mem, uint32_t (*now)(void));
void async_frame(void);
void async_stats(uint32_t *fps10, uint32_t *cpu10);

#ifdef __cplusplus
}
#endif

#endif
