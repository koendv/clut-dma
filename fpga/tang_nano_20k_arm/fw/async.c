/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  async.c: an auto gauge, the whole screen sent in one clut_dma request,
  interrupt driven: the CPU sleeps while the screen is sent.

  The gauge is adapted from 06-Auto-Guage of nishad2m8/T-Display-S3-YT
  (https://github.com/nishad2m8/T-Display-S3-YT), an LVGL gauge on a
  LilyGO T-Display S3: a band that rises from thin and flat at the left to
  thick at the right, the speed in large digits. Here in 1 bpp with colors
  per 8x8 tile, landscape 280x240, drawn with u8g2.

  The CLUT does the animation. The gauge is drawn once: 11 segments, each in
  its own three tile columns and its own CLUT entry. A segment is lit or
  dim by its CLUT entry alone, without drawing. Entry 0 is white on black
  (digits, text), 1 orange and 2 red on black (the scale numbers of those
  zones), 3..13 the segments. Each segment has its number (0, 2 .. 20, x10
  km/h, as the original) in the tile row above it, in tiles of their own;
  the numbers do not change. A segment is lit when the speed is past its
  number.

  Per frame, between two sends: the CLUT entries of the segments that
  changed, the speed digits if the speed changed, FPS and CPU % if they
  changed. Then one request for the whole screen, and the CPU sleeps (wfi)
  until the clut_dma interrupt. Only a few digits are drawn per frame:
  that is why the CPU % is low.

  One display, one clut_dma. The buffer, tile map and CLUT in fbmem (the
  full-screen layout of testimg.h, 8,400 bytes either way round). Landscape:
  MADCTL with MV (ASYNC_MADCTL, main.c), the u8g2 ST7789 driver for 280x240
  for the size. The window commands are sent here: the 280 columns at
  column 20 of the ST7789.
*/

#include <stdint.h>
#include <string.h>
#include "u8g2.h"
#include <hw.h>                  /* the board's: -I. first, not this directory */
#include "testimg.h"

void fault(const char *what);

#define AW       280             /* landscape */
#define AH       240
#define AXOFS    20              /* the 280 columns from column 20 of the ST7789 */
#define SEGS     11              /* CLUT entries 3..13 */
#define SEG_W    24              /* pixels, three tile columns */
#define SEG_X0   8               /* x of segment 0 */
#define SEG_KMH  20              /* km/h per segment */
#define LABEL_GAP 8              /* number baseline above the segment top */
#define IDX_WHITE  0
#define IDX_ORANGE 1
#define IDX_RED    2
#define IDX_SEG    3

#define RGB_WHITE  0xFFFFFFu
#define RGB_ORANGE 0xFF9020u
#define RGB_RED    0xFF2010u
#define RGB_DIM    0x303030u
#define RGB_BLACK  0x000000u

#define SPEED_X  8               /* the speed digits, baseline */
#define SPEED_Y  58
#define PERF_X   198             /* FPS and CPU %, bottom right */
#define PERF_Y   218             /* baseline of the first line */

static u8g2_t u8g2;
static uint8_t *base;            /* fbmem */
static uint32_t (*clock_now)(void);
static volatile uint8_t busy;    /* a send in progress; cleared by the interrupt */
static volatile uint8_t dma_err;

static uint8_t seg_lit[SEGS];
static int shown_speed = -1, shown_fps = -1, shown_cpu = -1;
static uint32_t t_frame, t_sleep, ms_acc, ms_time, sends;
static uint32_t fps256, cpu256;  /* smoothed over about 64 frames, 1/256 */

/* ------------------------------------------------------------------ */
/* the interrupt: the screen is sent */

static void irq_done(void)
{
  if (DMA_STATUS & 4)
    dma_err = 1;
  DMA_STATUS = 6;                /* clear DONE and ERR: the interrupt goes low */
  (void)DMA_STATUS;              /* the write is done before the return */
  while (SPI_STATUS & 1)         /* the last byte, at most a few us */
    ;
  SPI_CTRL = (SPI_CTRL & ~SPI_STREAM) | SPI_CS_N;
  busy = 0;
}

#if defined(__riscv)
/* GoRV32: clut_dma on EXT_INT[0], PLIC source 12. Interrupts stay off
   (mstatus.MIE 0): wfi wakes on the pending interrupt all the same, and
   the wait loop claims it, clears clut_dma and completes it. No trap
   handler: enabling MIE for one instruction between two wfi was too short
   on GoRV32, the pending interrupt was not taken */
#define PLIC_PRIORITY(s) REG(0xE4000000u + 4u * (s))
#define PLIC_ENABLE      REG(0xE4002000u)
#define PLIC_THRESHOLD   REG(0xE4200000u)
#define PLIC_CLAIM       REG(0xE4200004u)
#define PLIC_CLUT_DMA    12u

static void irq_init(void)
{
  uint32_t src;

  PLIC_PRIORITY(PLIC_CLUT_DMA) = 1;
  PLIC_THRESHOLD = 0;
  PLIC_ENABLE |= 1u << PLIC_CLUT_DMA;
  while ((src = PLIC_CLAIM) != 0)   /* an old request: claimed and completed */
    PLIC_CLAIM = src;
  __asm volatile ("csrs mie, %0" :: "r"(1u << 11));   /* MEIE: wfi wakes on it */
}

/* sleep until the send is done: wfi, then the interrupt, if it is there */
static void wait_done(void)
{
  uint32_t src;

  while (busy) {
    __asm volatile ("wfi");
    src = PLIC_CLAIM;
    if (src == PLIC_CLUT_DMA)
      irq_done();
    if (src)
      PLIC_CLAIM = src;          /* complete */
  }
}
#else
/* Cortex-M1: clut_dma on EXTINT[0], IRQ 10 (vector 26, start.c) */
#define NVIC_ISER REG(0xE000E100u)
#define NVIC_ICPR REG(0xE000E280u)
#define IRQ_CLUT_DMA 10u

void clut_dma_irq(void)
{
  irq_done();
}

static void irq_init(void)
{
  NVIC_ICPR = 1u << IRQ_CLUT_DMA;
  NVIC_ISER = 1u << IRQ_CLUT_DMA;
}

/* sleep until the send is done. wfi wakes on a pending interrupt with
   PRIMASK set; interrupts are on only between two wfi */
static void wait_done(void)
{
  __asm volatile ("cpsid i");
  while (busy) {
    __asm volatile ("wfi");
    __asm volatile ("cpsie i");
    __asm volatile ("isb");
    __asm volatile ("cpsid i");
  }
  __asm volatile ("cpsie i");
}
#endif

/* ------------------------------------------------------------------ */
/* the send: window, then one request, then back to the caller */

static void lcd_cmd(uint32_t ctrl, uint8_t c)
{
  while (SPI_STATUS & 1)
    ;
  SPI_CTRL = ctrl | SPI_CS_N;
  SPI_CTRL = ctrl;               /* chip select low, DC low: command */
  SPI_DATA = c;
  while (SPI_STATUS & 1)
    ;
  SPI_CTRL = ctrl | SPI_DC;
}

static void send_screen(void)
{
  uint32_t ctrl = SPI_CTRL & ~(SPI_DC | SPI_CS_N | SPI_STREAM);   /* reset, backlight */

  lcd_cmd(ctrl, 0x2A);           /* CASET AXOFS..AXOFS + 279 */
  SPI_DATA = 0; SPI_DATA = AXOFS;
  SPI_DATA = (AXOFS + AW - 1) >> 8; SPI_DATA = (AXOFS + AW - 1) & 0xFF;
  lcd_cmd(ctrl, 0x2B);           /* RASET 0..239 */
  SPI_DATA = 0; SPI_DATA = 0; SPI_DATA = 0; SPI_DATA = AH - 1;
  lcd_cmd(ctrl, 0x2C);           /* RAMWR */
  while (SPI_STATUS & 1)
    ;
  SPI_CTRL = ctrl | SPI_DC | SPI_STREAM;
  DMA_FB_START = (uint32_t)(base + FB_OFS);
  DMA_MAP_START = (uint32_t)(base + MAP_OFS);
  DMA_MAP_NIBBLE = 0;
  DMA_CLUT = (uint32_t)(base + CLUT_OFS);
  DMA_LINE_STRIDE = AW;          /* a tile row of the u8g2 buffer: 35 tiles */
  DMA_SIZE = (AH << 16) | AW;
  busy = 1;
  sends++;
  DMA_CTRL = DMA_FORMAT(DMA_RGB444) | DMA_VERT | DMA_IRQ_EN | DMA_START;
}

/* ------------------------------------------------------------------ */
/* the gauge */

/* the band, as the original: flat and thin at the left, an S-curve up,
   thick and flat again at the right. smoothstep from a to b of u, all in
   1/1024 of the band width; returns 0..1024 */
static int ramp(int x, int a, int b)
{
  int u = (x - SEG_X0) * 1024 / (SEGS * SEG_W), t;

  t = u <= a ? 0 : u >= b ? 1024 : (u - a) * 1024 / (b - a);
  return t * t / 1024 * (3 * 1024 - 2 * t) / 1024;
}

/* top edge from y 207 to 80, bottom edge from 216 to 132: 9 to 52 thick */
static int band_top(int x)
{
  return 207 - 127 * ramp(x, 154, 920) / 1024;
}

static int band_bottom(int x)
{
  return 216 - 84 * ramp(x, 225, 900) / 1024;
}

/* the zones of the original: white up to 10, orange 12..16, red 18..20 */
static uint8_t seg_zone(int i)
{
  int n = i * 2;

  return n >= 18 ? IDX_RED : n >= 12 ? IDX_ORANGE : IDX_WHITE;
}

static uint32_t zone_rgb(uint8_t zone)
{
  return zone == IDX_RED ? RGB_RED : zone == IDX_ORANGE ? RGB_ORANGE : RGB_WHITE;
}

static void draw_gauge(u8g2_t *u)
{
  int i;
  char s[4];

  u8g2_SetFont(u, u8g2_font_helvB14_tn);
  for (i = 0; i < SEGS; i++) {
    int x0 = SEG_X0 + SEG_W * i, xa = x0, xb = x0 + SEG_W - 3;   /* a 2-pixel gap */
    int ta = band_top(xa), tb = band_top(xb), ba = band_bottom(xa), bb = band_bottom(xb);
    uint8_t tx = (uint8_t)(x0 / 8), ty0 = (uint8_t)(tb / 8), ty1 = (uint8_t)(ba / 8);
    int n = i * 2;               /* the scale, x10 km/h */

    /* the segment: a quadrilateral, two triangles, in its own tile columns */
    u8g2_DrawTriangle(u, xa, ta, xb, tb, xb, bb);
    u8g2_DrawTriangle(u, xa, ta, xb, bb, xa, ba);
    u8g2_SetClutRegion(u, tx, ty0, SEG_W / 8, (uint8_t)(ty1 - ty0 + 1), (uint8_t)(IDX_SEG + i));

    /* its number, LABEL_GAP above the segment: the same distance for all,
       and always in tiles above those of the segment (a top edge is at
       most 7 rows into its tile); the number's tiles in its zone color */
    s[0] = n >= 10 ? (char)('0' + n / 10) : (char)('0' + n);
    s[1] = n >= 10 ? (char)('0' + n % 10) : 0;
    s[2] = 0;
    {
      int base_y = tb - LABEL_GAP, top_y = base_y - u8g2_GetAscent(u) + 1;

      u8g2_DrawStr(u, x0 + (SEG_W - u8g2_GetStrWidth(u, s)) / 2, base_y, s);
      u8g2_SetClutRegion(u, tx, (uint8_t)(top_y / 8), SEG_W / 8,
                         (uint8_t)(base_y / 8 - top_y / 8 + 1), seg_zone(i));
    }
  }
  u8g2_SetFont(u, u8g2_font_profont17_tr);
  u8g2_DrawStr(u, SPEED_X, SPEED_Y + 20, "km/h");
}

/* the speed, time based: accelerate, cruise, brake, again; ms in the cycle */
static const struct { uint16_t ms, kmh; } drive[] = {
  { 0, 0 }, { 6000, 196 }, { 9000, 183 }, { 12500, 50 }, { 15000, 124 },
  { 18000, 124 }, { 21000, 0 }, { 22000, 0 },
};
#define DRIVE_MS 22000u

static int speed_at(uint32_t ms)
{
  unsigned i;

  ms %= DRIVE_MS;
  for (i = 1; ms > drive[i].ms; i++)
    ;
  return drive[i - 1].kmh + (int)(drive[i].kmh - drive[i - 1].kmh) *
         (int)(ms - drive[i - 1].ms) / (int)(drive[i].ms - drive[i - 1].ms);
}

/* round glyphs (0, 3, 5, 6, 8, 9, %) go below the baseline: the box that
   clears the old text goes down to OVERSHOOT rows under the baseline */
#define OVERSHOOT 2

static void draw_number(u8g2_t *u, int x, int y, int w, int h, const char *s)
{
  u8g2_SetDrawColor(u, 0);
  u8g2_DrawBox(u, x, y - h, w, h + 1 + OVERSHOOT);
  u8g2_SetDrawColor(u, 1);
  u8g2_DrawStr(u, x, y, s);
}

/* the speed: three digits, right aligned, blank in front; each digit
   centered in a cell of the widest digit, so the digits stay in place.
   only the cells that changed are cleared and drawn: from one frame to
   the next, mostly the last digit */
static void draw_speed(u8g2_t *u, int speed)
{
  static char shown[3];          /* the cells on the screen: digit, ' ', or 0 at the start */
  char cell[3], d[2] = { 0, 0 };
  int cw, j;

  for (j = 2; j >= 0; j--) {
    cell[j] = (j == 2 || speed) ? (char)('0' + speed % 10) : ' ';
    speed /= 10;
  }
  u8g2_SetFont(u, u8g2_font_logisoso50_tn);
  cw = u8g2_GetMaxCharWidth(u);
  for (j = 0; j < 3; j++) {
    if (cell[j] == shown[j])
      continue;
    shown[j] = cell[j];
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, SPEED_X + j * cw, SPEED_Y - 52, cw, 52 + 1 + OVERSHOOT);
    u8g2_SetDrawColor(u, 1);
    if (cell[j] != ' ') {
      d[0] = cell[j];
      u8g2_DrawStr(u, SPEED_X + j * cw + (cw - u8g2_GetStrWidth(u, d)) / 2, SPEED_Y, d);
    }
  }
}

static char *dec(char *s, unsigned v)
{
  char t[6];
  int n = 0;

  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 6);
  while (n)
    *s++ = t[--n];
  *s = 0;
  return s;
}

/* the changes of a frame: segments (CLUT), speed, FPS and CPU % */
static void update(void)
{
  u8g2_t *u = &u8g2;
  int speed = speed_at(ms_time), i, fps = (int)((fps256 + 128) / 256), cpu = (int)((cpu256 + 128) / 256);
  char s[16], *p;

  for (i = 0; i < SEGS; i++) {
    uint8_t lit = speed > i * SEG_KMH;   /* past its number */

    if (lit != seg_lit[i]) {
      seg_lit[i] = lit;
      u8g2_SetClutColor(u, (uint8_t)(IDX_SEG + i), lit ? zone_rgb(seg_zone(i)) : RGB_DIM, RGB_BLACK);
    }
  }
  if (speed != shown_speed) {
    shown_speed = speed;
    draw_speed(u, speed);
  }
  u8g2_SetFont(u, u8g2_font_profont17_tr);
  if (fps != shown_fps) {
    shown_fps = fps;
    p = dec(s, (unsigned)fps);
    strcpy(p, " FPS");
    draw_number(u, PERF_X, PERF_Y, AW - PERF_X, 14, s);
  }
  if (cpu != shown_cpu) {
    shown_cpu = cpu;
    p = dec(s, (unsigned)cpu);
    strcpy(p, "% CPU");
    draw_number(u, PERF_X, PERF_Y + 18, AW - PERF_X, 14, s);
  }
}

static uint8_t u8x8_none(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
  return 1;
}

/* ------------------------------------------------------------------ */

void async_init(volatile uint8_t *mem, uint32_t (*now)(void))
{
  u8g2_t *u = &u8g2;
  int i;

  base = (uint8_t *)(uintptr_t)mem;
  clock_now = now;
  /* a full buffer in fbmem; the ST7789 driver for 280x240 only for the
     size: the screen is sent here, not by u8g2 */
  u8g2_SetupDisplay(u, u8x8_d_st7789_240x280_rot, u8x8_cad_011, u8x8_none, u8x8_none);
  u8g2_SetupBuffer(u, base + FB_OFS, AH / 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  memset(base + MAP_OFS, 0, u8x8_clut_map_size(AW, AH));
  u8g2_SetClutMap(u, base + MAP_OFS);
  _Static_assert((CLUT_OFS & 63u) == 0, "clut_dma: clut 64-byte aligned");
  u8g2_SetClutTable(u, (const uint16_t (*)[2])(void *)(base + CLUT_OFS));
  u8g2_SetClutColor(u, IDX_WHITE, RGB_WHITE, RGB_BLACK);
  u8g2_SetClutColor(u, IDX_ORANGE, RGB_ORANGE, RGB_BLACK);
  u8g2_SetClutColor(u, IDX_RED, RGB_RED, RGB_BLACK);
  for (i = 0; i < SEGS; i++)
    u8g2_SetClutColor(u, (uint8_t)(IDX_SEG + i), RGB_DIM, RGB_BLACK);
  u8g2_SetFontMode(u, 0);
  u8g2_ClearBuffer(u);
  draw_gauge(u);
  irq_init();
  t_frame = clock_now();
}

/* a frame: sleep until the last one is sent, then the changes, then the
   next send. returns while it is sent */
void async_frame(void)
{
  uint32_t t0 = clock_now(), t, frame;

  wait_done();
  t = clock_now();
  if (dma_err)
    fault("clut_dma: bus error");
  t_sleep = t - t0;
  frame = t - t_frame;
  t_frame = t;
  /* the drive cycle in ms, from the frame times */
  ms_acc += frame;
  ms_time += ms_acc / (HCLK_HZ / 1000u);
  ms_acc %= HCLK_HZ / 1000u;
  /* FPS and CPU %, smoothed over about 64 frames; from the second whole
     frame on: the first did not wait for a send */
  if (sends >= 2 && frame) {
    uint32_t f256 = (uint32_t)((uint64_t)HCLK_HZ * 256u / frame);
    uint32_t c256 = (uint32_t)((uint64_t)(frame - t_sleep) * 25600u / frame);

    fps256 = fps256 ? fps256 + (uint32_t)((int32_t)(f256 - fps256) / 64) : f256;
    cpu256 = cpu256 ? cpu256 + (uint32_t)((int32_t)(c256 - cpu256) / 64) : c256;
  }
  update();
  send_screen();
}

/* FPS and CPU %, in tenths, for the uart */
void async_stats(uint32_t *fps10, uint32_t *cpu10)
{
  *fps10 = fps256 * 10u / 256u;
  *cpu10 = cpu256 * 10u / 256u;
}
