/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  main.c: first test of clut_dma on the Tang Nano 20K, with a WeAct ST7789
  240x280 display. Output on UART0, 115200 baud, /dev/ttyUSB1.
    1. the display, from the cpu: init, three full screens in one color
    2. clut_dma: a test pattern, whole screens in rgb565, rgb444, rgb666,
       then rectangles with a second clut
  The test images and requests are in testimg.c; "make sim" shows the image
  the display ought to show, from a simulation of the RTL.
  The timing mark (gpio bit 1) is high during each transfer. Cycle counts are
  hclk cycles, from START to DONE.
*/

#include <stdint.h>
#include "hw.h"
#include "testimg.h"

#ifndef LCD_DIV
#define LCD_DIV    1u             /* sck = 54 MHz / 2 = 27 MHz, the fastest */
#endif

#define FB  (FBMEM + FB_OFS)
#define MAP (FBMEM + MAP_OFS)

#ifndef LCD_BRIGHT
#define LCD_BRIGHT 128u           /* backlight 128/256, 50% */
#endif
#define BL (SPI_BL | (LCD_BRIGHT << 8))
#define ON (SPI_RST_N | BL)        /* chip select low, out of reset, backlight on */

/* ------------------------------------------------------------------ */
/* uart */

static void uart_init(void)
{
  UART0_BAUDDIV = (HCLK_HZ + 115200 / 2) / 115200;
  UART0_CTRL = 3;                /* tx and rx */
}

#ifdef MUI_TEST
/* a received character, or -1 */
static int getch(void)
{
  return (UART0_STATE & 2) ? (int)(UART0_DATA & 0xFF) : -1;
}
#endif

static void putch(char c)
{
  while (UART0_STATE & 1)
    ;
  UART0_DATA = (uint8_t)c;
}

static void puts_(const char *s)
{
  while (*s) {
    if (*s == '\n')
      putch('\r');
    putch(*s++);
  }
}

static void putdec(uint32_t v)
{
  char b[11];
  int i = 0;

  do {
    b[i++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (i)
    putch(b[--i]);
}

static void puthex(uint32_t v)
{
  int i;

  for (i = 28; i >= 0; i -= 4)
    putch("0123456789abcdef"[(v >> i) & 15]);
}

void fault(const char *what)
{
  puts_("\n*** ");
  puts_(what);
  puts_("\n");
  for (;;)
    ;
}

/* ------------------------------------------------------------------ */
/* time: SysTick at hclk, 24 bits, extended by the wrap interrupt */

static volatile uint32_t ticks_hi;

void systick_handler(void)
{
  ticks_hi++;
}

static void time_init(void)
{
  SYST_RVR = 0xFFFFFF;
  SYST_CVR = 0;
  SYST_CSR = 7;                  /* enable, interrupt, hclk */
  __asm volatile ("cpsie i");
}

static uint32_t now(void)
{
  uint32_t hi, lo;

  do {
    hi = ticks_hi;
    lo = SYST_CVR;
  } while (hi != ticks_hi);
  return (hi << 24) + (0xFFFFFFu - lo);
}

static void delay_ms(uint32_t ms)
{
  uint32_t t0 = now();

  while (now() - t0 < ms * (HCLK_HZ / 1000))
    ;
}

/* ------------------------------------------------------------------ */
/* display, through spi_lcd */

static void spi_wait(void)
{
  while (SPI_STATUS & 1)
    ;
}

/* chip select high between commands, as u8g2 does: the display stays black
   if chip select is low all the time */
static void lcd_cmd(uint8_t c)
{
  spi_wait();
  SPI_CTRL = ON | SPI_CS_N;
  SPI_CTRL = ON;                 /* chip select low, DC low: command */
  SPI_DATA = c;
  spi_wait();
  SPI_CTRL = ON | SPI_DC;        /* DC high: data */
}

static void lcd_data(uint8_t d)
{
  SPI_DATA = d;                  /* PREADY low while the data register is full */
}

static void lcd_cmd1(uint8_t c, uint8_t d)
{
  lcd_cmd(c);
  lcd_data(d);
}

static void lcd_window(unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
  y0 += LCD_YOFS;
  y1 += LCD_YOFS;
  lcd_cmd(0x2A);                 /* CASET */
  lcd_data((uint8_t)(x0 >> 8)); lcd_data((uint8_t)x0);
  lcd_data((uint8_t)(x1 >> 8)); lcd_data((uint8_t)x1);
  lcd_cmd(0x2B);                 /* RASET */
  lcd_data((uint8_t)(y0 >> 8)); lcd_data((uint8_t)y0);
  lcd_data((uint8_t)(y1 >> 8)); lcd_data((uint8_t)y1);
  lcd_cmd(0x2C);                 /* RAMWR */
}

/* init sequence of u8x8_d_st7789.c in u8g2, colors rgb565 */
static void lcd_init(void)
{
  SPI_DIV = LCD_DIV;
  SPI_CTRL = SPI_CS_N;           /* reset low, chip select high */
  delay_ms(10);
  SPI_CTRL = SPI_CS_N | SPI_RST_N;
  delay_ms(120);
  lcd_cmd(0x01);                 /* software reset */
  delay_ms(150);
  lcd_cmd(0x11);                 /* sleep out */
  delay_ms(120);
  lcd_cmd1(0x3A, 0x05);          /* COLMOD: rgb565 */
  lcd_cmd1(0x36, LCD_MADCTL);    /* MADCTL */
  lcd_cmd(0x21);                 /* display inversion on */
  lcd_cmd(0x13);                 /* normal display mode on */
  lcd_cmd(0x29);                 /* display on */
  delay_ms(10);
  spi_wait();
}

/* a whole screen in one rgb565 color, from the cpu */
static void lcd_fill(uint16_t color)
{
  uint32_t n;

  lcd_window(0, 0, LCD_W - 1, LCD_H - 1);
  for (n = 0; n < LCD_W * LCD_H; n++) {
    lcd_data((uint8_t)(color >> 8));
    lcd_data((uint8_t)color);
  }
  spi_wait();
}

#ifdef LCD_TEST
/* ------------------------------------------------------------------ */
/* display only, without clut_dma: a fixed byte stream from the cpu, with chip
   select high between commands, as u8g2 does. CMD a command, DLY a delay in
   ms, other entries parameter bytes */

#define CMD(c) (0x100u | (c))
#define DLY(ms) (0x200u | (ms))
#define END 0x400u

static const uint16_t lcd_stream[] = {
  CMD(0x01), DLY(150),                    /* software reset */
  CMD(0x11), DLY(250), DLY(250),          /* sleep out */
  CMD(0x3A), 0x55,                        /* COLMOD: rgb565 */
  CMD(0x36), LCD_MADCTL,                  /* MADCTL */
  CMD(0x21),                              /* display inversion on */
  CMD(0x13), DLY(10),                     /* normal display mode on */
  CMD(0x29), DLY(100),                    /* display on */
  END
};

/* the whole screen, then RAMWR */
static const uint16_t lcd_screen[] = {
  CMD(0x2A), 0x00, 0x00, 0x00, LCD_W - 1, /* CASET 0..239 */
  CMD(0x2B), 0x00, LCD_YOFS, (LCD_YOFS + LCD_H - 1) >> 8, (LCD_YOFS + LCD_H - 1) & 0xFF,
  CMD(0x2C),                              /* RAMWR */
  END
};

static void lcd_send(const uint16_t *p)
{
  for (; *p != END; p++) {
    if (*p & 0x100u) {
      spi_wait();
      SPI_CTRL = SPI_RST_N | BL | SPI_CS_N;   /* chip select high */
      SPI_CTRL = SPI_RST_N | BL;              /* low, command */
      SPI_DATA = *p & 0xFFu;
      spi_wait();
      SPI_CTRL = SPI_RST_N | BL | SPI_DC;     /* parameters */
    } else if (*p & 0x200u) {
      spi_wait();
      delay_ms(*p & 0xFFu);
    } else
      SPI_DATA = *p;
  }
  spi_wait();
  SPI_CTRL = SPI_RST_N | BL | SPI_CS_N;
}

/* test without clut_dma: reset, init stream, then full screens in a loop */
static void lcd_test(void)
{
  static const uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
  unsigned i = 0;
  uint32_t n;

  puts_("\nlcd test: no clut_dma, sck 54 MHz / 32\n");
  SPI_DIV = 16;
  SPI_CTRL = SPI_CS_N | BL;                 /* reset low */
  delay_ms(20);
  SPI_CTRL = SPI_CS_N | BL | SPI_RST_N;
  delay_ms(150);
  lcd_send(lcd_stream);
  puts_("init sent\n");
  for (;;) {
    uint16_t c = colors[i];

    lcd_send(lcd_screen);
    SPI_CTRL = SPI_RST_N | BL | SPI_DC;    /* chip select low, data */
    for (n = 0; n < LCD_W * LCD_H; n++) {
      SPI_DATA = c >> 8;
      SPI_DATA = c & 0xFF;
    }
    spi_wait();
    SPI_CTRL = SPI_RST_N | BL | SPI_CS_N;
    puts_("color ");
    puthex(c);
    puts_("\n");
    delay_ms(250); delay_ms(250); delay_ms(250); delay_ms(250);
    i = (i + 1) % 5;
  }
}
#endif

/* ------------------------------------------------------------------ */
/* clut_dma */

/* rectangle of w x h pixels, top left at tile (tx, ty), its bitmap at fb (a
   pixel row of COLS bytes), clut at fbmem offset clut; returns hclk cycles */
static uint32_t request(unsigned fmt, uint32_t fb, unsigned tx, unsigned ty, unsigned w, unsigned h, uint32_t clut)
{
  uint32_t t0, t;

  lcd_window(8 * tx, 8 * ty, 8 * tx + w - 1, 8 * ty + h - 1);
  spi_wait();
  SPI_CTRL = ON | SPI_DC | SPI_STREAM;
  DMA_FB_START = fb;
  DMA_MAP = MAP;
  DMA_CLUT = FBMEM + clut;
  DMA_LINE_OFFSET = COLS - (w + 7) / 8;
  DMA_FIRST = ty * COLS + tx;
  DMA_SIZE = (h << 16) | w;
  GPIO = GPIO_LED | GPIO_MARK;
  t0 = now();
  DMA_CTRL = (fmt << 1) | 1;
  while (!(DMA_STATUS & 2))
    ;
  t = now() - t0;
  GPIO = GPIO_LED;
  DMA_STATUS = 2;                /* clear DONE */
  spi_wait();
  SPI_CTRL = ON | SPI_DC;
  return t;
}

static void report(const char *what, uint32_t cycles, uint32_t bytes)
{
  puts_(what);
  puts_(": ");
  putdec(cycles);
  puts_(" cycles, ");
  putdec(bytes);
  puts_(" bytes, ");
  putdec(cycles / bytes);
  puts_(".");
  putdec(cycles * 10 / bytes % 10);
  puts_(" cycles per byte\n");
}

#if defined(CLUTCOLOR_TEST) || defined(CLUTCLOCK_TEST) || defined(GRAPHICS_TEST) || defined(MUI_TEST)
/* a display update of a demo: a rectangle of tiles, rgb444. the requests and
   their hclk cycles are counted; each one is reported if send_verbose */
static uint32_t send_cnt, send_cycles;
static int send_verbose = 1;

static void send_rect(uint16_t fb, uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th, const char *what)
{
  uint32_t w = tw * 8u, h = th * 8u;
  uint32_t t = request(FMT_RGB444, FBMEM + fb, tx, ty, w, h, CLUT_OFS);

  send_cnt++;
  send_cycles += t;
  if (send_verbose)
    report(what, t, w * h * 3 / 2);
}

#if defined(CLUTCLOCK_TEST) || defined(GRAPHICS_TEST)
/* a frame: requests, their clut_dma cycles, cycles to draw and send */
static void frame_report(uint32_t total)
{
  puts_("frame: ");
  putdec(send_cnt);
  puts_(" requests, clut_dma ");
  putdec(send_cycles);
  puts_(" cycles, draw and send ");
  putdec(total);
  puts_(" cycles\n");
  send_cnt = 0;
  send_cycles = 0;
}
#endif
#endif

/* the steps of a test, from testimg.c */
static void run(const struct step *s)
{
  for (; s->kind != STEP_END; s++)
    switch (s->kind) {
    case STEP_REQ: {
      uint32_t px = (uint32_t)s->w * s->h;
      uint32_t bytes = s->fmt == FMT_RGB444 ? px * 3 / 2 : s->fmt == FMT_RGB666 ? px * 3 : px * 2;
      report(s->name, request(s->fmt, FB + 8 * s->ty * COLS + s->tx, s->tx, s->ty, s->w, s->h, s->clut), bytes);
      break;
    }
    case STEP_COLMOD:
      lcd_cmd1(0x3A, s->fmt);
      break;
    case STEP_DELAY:
      delay_ms(s->w);
      break;
    }
}

int main(void)
{
  uint32_t t;

  uart_init();
  time_init();
  GPIO = GPIO_LED;

#ifdef LCD_TEST
  lcd_test();
#endif

#ifdef CLUTCOLOR_TEST
  puts_("\nClutColor, u8g2\n");
  lcd_init();
  lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
  clutcolor_init((volatile uint8_t *)FBMEM, send_rect);
  for (;;) {
    delay_ms(1000);
    clutcolor_tick((volatile uint8_t *)FBMEM, send_rect);
  }
#endif

#ifdef MUI_TEST
  {
    int c, esc = 0;

    puts_("\ndemo_mui, u8g2: w s or up down: field, a d or left right: value, space enter: select\n");
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    mui_demo_init((volatile uint8_t *)FBMEM, send_rect);
    for (;;) {
      c = getch();
      if (c < 0)
        continue;
      /* arrow keys: ESC [ A..D */
      if (esc == 0 && c == 0x1B) {
        esc = 1;
        continue;
      }
      if (esc == 1) {
        esc = c == '[' ? 2 : 0;
        continue;
      }
      if (esc == 2) {
        esc = 0;
        c = c == 'A' ? 'w' : c == 'B' ? 's' : c == 'C' ? 'd' : c == 'D' ? 'a' : 0;
      }
      mui_demo_key((volatile uint8_t *)FBMEM, send_rect, (char)c);
    }
  }
#endif

#ifdef GRAPHICS_TEST
  {
    uint32_t t0;
    uint16_t pause;

    puts_("\ndemo_graphics, u8g2, buffer of ");
    putdec(PAGE_ROWS);
    puts_(" tile rows\n");
    send_verbose = 0;
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    graphics_init((volatile uint8_t *)FBMEM);
    for (;;) {
      t0 = now();
      pause = graphics_step((volatile uint8_t *)FBMEM, send_rect);
      frame_report(now() - t0);
      delay_ms(pause);
    }
  }
#endif

#ifdef CLUTCLOCK_TEST
  {
    uint32_t next, t0;

    puts_("\nClutClock, u8g2, buffer of ");
    putdec(PAGE_ROWS);
    puts_(" tile rows\n");
    send_verbose = 0;
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    clutclock_init((volatile uint8_t *)FBMEM, send_rect);
    send_cnt = send_cycles = 0;
    next = now();
    for (;;) {
      next += HCLK_HZ;             /* every second, without drift */
      while ((int32_t)(now() - next) < 0)
        ;
      t0 = now();
      clutclock_tick((volatile uint8_t *)FBMEM, send_rect);
      frame_report(now() - t0);
    }
  }
#endif

#ifdef PALETTE_TEST
  puts_("\nClutPalette, u8g2\n");
  lcd_init();
  palette((volatile uint8_t *)FBMEM);
  run(palette_steps);
  for (;;)
    ;
#endif

#ifdef BORDER_TEST
  puts_("\nborder test\n");
  lcd_init();
  border((volatile uint8_t *)FBMEM);
  run(border_steps);
  for (;;)
    ;
#endif

#ifdef SCL_TEST
  /* SCL toggles without pause at the slowest clock, SDA high, chip select
     high: a multimeter on SCL shows about half the supply */
  puts_("\nscl test: sck 54 MHz / 510, 50% duty, cs high\n");
  SPI_DIV = 255;
  SPI_CTRL = SPI_CS_N | SPI_RST_N | BL;
  for (;;)
    SPI_DATA = 0xFF;             /* PREADY low while the data register is full */
#endif

  puts_("\nclut_dma test, cpuid ");
  puthex(REG(0xE000ED00));
  puts_("\n");

  lcd_init();
  puts_("display init done\n");

  t = now(); lcd_fill(0xF800); report("cpu fill red", now() - t, LCD_W * LCD_H * 2);
  delay_ms(500);
  t = now(); lcd_fill(0x07E0); report("cpu fill green", now() - t, LCD_W * LCD_H * 2);
  delay_ms(500);
  t = now(); lcd_fill(0x001F); report("cpu fill blue", now() - t, LCD_W * LCD_H * 2);
  delay_ms(500);

  pattern((volatile uint8_t *)FBMEM);
  puts_("pattern in fbmem\n");
  run(pattern_steps);
  puts_("done\n");

  for (;;) {
    GPIO = 0;
    delay_ms(500);
    GPIO = GPIO_LED;
    delay_ms(500);
  }
}
