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
/* the u8g2 demos: they send through u8g2, with the byte function u8x8_byte_lcd */
#if defined(PALETTE_TEST) || defined(CLUTCOLOR_TEST) || defined(CLUTCLOCK_TEST) || defined(GRAPHICS_TEST) || defined(MUI_TEST) || defined(SOLDER_TEST)
#define U8G2_SEND
#include "u8x8.h"
#endif

#ifndef LCD_DIV
#define LCD_DIV    1u             /* sck = 54 MHz / 2 = 27 MHz, the fastest */
#endif

#ifndef STARS_FPS
#define STARS_FPS  50u            /* falling stars: frames per second */
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

#if defined(MUI_TEST) || defined(SOLDER_TEST)
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

/* rectangle of w x h pixels, top left at tile (tx, ty), its first tile at fb
   (horizontal: a pixel row of COLS bytes; vertical, FMT_VERT: tiles of 8
   bytes, a tile row of 8 * COLS bytes), clut at fbmem offset clut; returns
   hclk cycles */
static uint32_t request(unsigned fmt, uint32_t fb, unsigned tx, unsigned ty, unsigned w, unsigned h, uint32_t clut)
{
  uint32_t t0, t;

  lcd_window(8 * tx, 8 * ty, 8 * tx + w - 1, 8 * ty + h - 1);
  spi_wait();
  SPI_CTRL = ON | SPI_DC | SPI_STREAM;
  DMA_FB_START = fb;
  DMA_MAP_START = MAP + (ty * COLS + tx) / 2;
  DMA_MAP_NIBBLE = (ty * COLS + tx) & 1;
  DMA_CLUT = FBMEM + clut;
  DMA_LINE_STRIDE = (fmt & FMT_VERT) ? 8 * COLS : COLS;
  DMA_SIZE = (h << 16) | w;
  GPIO = GPIO_LED | GPIO_MARK;
  t0 = now();
  DMA_CTRL = ((fmt & 3u) << 1) | ((fmt & FMT_VERT) ? 0x10u : 0u) | ((fmt & FMT_NOMAP) ? 0x20u : 0u) | 1u;
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

#if defined(U8G2_SEND) || defined(STARS_TEST)
/* the requests of a demo and their hclk cycles */
static uint32_t send_cnt, send_cycles;
#endif

#ifdef STARS_TEST
/* a display update of the Adafruit demo: the bitmap in the horizontal
   layout, w x h pixels at tile (tx, ty), rgb444 */
static void send_row(uint8_t tx, uint8_t ty, uint16_t w, uint16_t h, const char *what)
{
  (void)what;
  send_cycles += request(FMT_RGB444, FB + 8u * ty * COLS + tx, tx, ty, w, h, CLUT_OFS);
  send_cnt++;
}
#endif

#ifdef U8G2_SEND
/* the u8x8 byte function of the demos that send through u8g2: the spi of
   spi_lcd, chip select low for a transfer, as the 4-wire spi of u8g2. a
   transfer is a tile row: the window commands of the u8g2 driver, then the
   pixels, from clut_dma (u8x8_clut_dma.c) or the cpu. the timing mark is
   high during a transfer. send_cnt: the transfers; send_cycles: their
   cycles, the cpu waiting for clut_dma or busy sending */
static uint32_t xfer_t0;

static uint8_t u8x8_byte_lcd(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  const uint8_t *p = (const uint8_t *)arg_ptr;

  (void)u8x8;
  switch (msg) {
  case U8X8_MSG_BYTE_SEND:
    while (arg_int--)
      SPI_DATA = *p++;           /* PREADY low while the data register is full */
    break;
  case U8X8_MSG_BYTE_SET_DC:
    spi_wait();
    SPI_CTRL = ON | (arg_int ? SPI_DC : 0u);
    break;
  case U8X8_MSG_BYTE_START_TRANSFER:
    xfer_t0 = now();
    send_cnt++;
    GPIO = GPIO_LED | GPIO_MARK;
    spi_wait();
    SPI_CTRL = ON | SPI_CS_N;
    SPI_CTRL = ON;               /* chip select low */
    break;
  case U8X8_MSG_BYTE_END_TRANSFER:
    spi_wait();
    SPI_CTRL = ON | SPI_CS_N;    /* chip select high */
    GPIO = GPIO_LED;
    send_cycles += now() - xfer_t0;
    break;
  default:
    break;
  }
  return 1;
}
#endif

#if defined(CLUTCOLOR_TEST) || defined(CLUTCLOCK_TEST) || defined(GRAPHICS_TEST)
/* a frame: transfers (tile rows), their cycles, cycles to draw and send */
static void frame_report(uint32_t total)
{
  puts_("frame: ");
  putdec(send_cnt);
  puts_(" requests, sending ");
  putdec(send_cycles);
  puts_(" cycles, draw and send ");
  putdec(total);
  puts_(" cycles\n");
  send_cnt = 0;
  send_cycles = 0;
}
#endif

#if defined(SOLDER_TEST) || defined(ASYNC_TEST)
/* the highest stack use: the free ram between .bss and the stack is filled
   with a pattern at the start; the lowest word that changed */
extern uint32_t _sbss[], _ebss[], _estack[];
#define STACK_FILL 0x5A5A5A5Au

static void stack_fill(void)
{
  uint32_t *p = _ebss, *sp;

  __asm__ volatile ("mov %0, sp" : "=r"(sp));
  while (p < sp - 16)
    *p++ = STACK_FILL;
}

static uint32_t stack_used(void)
{
  const uint32_t *p = _ebss;

  while (p < _estack && *p == STACK_FILL)
    p++;
  return (uint32_t)((const uint8_t *)_estack - (const uint8_t *)p);
}
#endif

/* the steps of a test, from testimg.c */
static void run(const struct step *s)
{
  for (; s->kind != STEP_END; s++)
    switch (s->kind) {
    case STEP_REQ: {
      uint32_t px = (uint32_t)s->w * s->h;
      unsigned f = s->fmt & 3u;
      uint32_t bytes = f == FMT_RGB444 ? px * 3 / 2 : f == FMT_RGB666 ? px * 3 : px * 2;
      uint32_t fb = (s->fmt & FMT_VERT) ? FB + 8 * (s->ty * COLS + s->tx) : FB + 8 * s->ty * COLS + s->tx;
      report(s->name, request(s->fmt, fb, s->tx, s->ty, s->w, s->h, s->clut), bytes);
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
  {
    uint32_t t0;

    puts_("\nClutColor, u8g2\n");
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    clutcolor_init((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
    send_cnt = send_cycles = 0;
    for (;;) {
      delay_ms(1000);
      t0 = now();
      clutcolor_tick();
      frame_report(now() - t0);
    }
  }
#endif

#ifdef MUI_TEST
  {
    int c, esc = 0;

    puts_("\ndemo_mui, u8g2: w s or up down: field, a d or left right: value, space enter: select\n");
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    mui_demo_init((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
    for (;;) {
      c = getch();
      if (c < 0)
        continue;
      /* arrow keys: ESC [ A..D, or ESC O A..D (application cursor mode) */
      if (esc == 0 && c == 0x1B) {
        esc = 1;
        continue;
      }
      if (esc == 1) {
        esc = (c == '[' || c == 'O') ? 2 : 0;
        continue;
      }
      if (esc == 2) {
        esc = 0;
        c = c == 'A' ? 'w' : c == 'B' ? 's' : c == 'C' ? 'd' : c == 'D' ? 'a' : 0;
      }
      mui_demo_key((char)c);
    }
  }
#endif

#ifdef SOLDER_TEST
  {
    uint32_t next, t0, ms = HCLK_HZ / 1000u;
    uint32_t frames = 0, total = 0, worst = 0, ticks = 0, report = 0, steps, waited = 0;
    int c, esc = 0;

    stack_fill();
    puts_("\nsoldering station, u8g2, buffer of ");
    putdec(PAGE_ROWS);
#ifdef SOLDER_COLOR
    puts_(" tile rows, color");
#else
    puts_(" tile rows, black and white");
#endif
#ifndef USE_CLUT_DMA
    puts_(", software: the cpu expands the colors (u8x8_clut_stream_rgb444)");
#endif
    puts_("\nw s or up down: set temperature, "
          "space enter: menu, e: sensor fault, b: benchmark; menu: w s field, a d value\n");
    puts_("ram: display ");
    putdec(FBMEM_USED);
    puts_(" bytes of fbmem, .bss ");
    putdec((uint32_t)((uint8_t *)_ebss - (uint8_t *)_sbss));
    puts_(" bytes\n");
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    solder_init((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
    next = now();
    for (;;) {
      c = getch();
      if (c >= 0) {
        /* arrow keys: ESC [ A..D, or ESC O A..D (application cursor mode) */
        if (esc == 0 && c == 0x1B)
          esc = 1;
        else if (esc == 1)
          esc = (c == '[' || c == 'O') ? 2 : 0;
        else {
          if (esc == 2)
            c = c == 'A' ? 'w' : c == 'B' ? 's' : c == 'C' ? 'd' : c == 'D' ? 'a' : 0;
          esc = 0;
          if (c == 'b') {
            /* benchmark: 50 frames of the screen shown, home or menu, back
               to back, the throughput */
            uint32_t n, w = 0;

            puts_("benchmark: 50 frames, a few seconds ...\n");
            t0 = now();
            for (n = 0; n < 50; n++) {
              send_cycles = 0;
              solder_frame();
              w += send_cycles;
            }
            t = now() - t0;
            puts_("benchmark: 50 frames in ");
            putdec(t / ms);
            puts_(" ms, ");
            putdec(t / 50 / (ms / 10) / 10);
            puts_(".");
            putdec(t / 50 / (ms / 10) % 10);
#ifndef USE_CLUT_DMA
            puts_(" ms per frame, cpu busy with the display ");
#else
            puts_(" ms per frame, cpu waits for the display ");
#endif
            putdec(w / 50 / (ms / 10) / 10);
            puts_(".");
            putdec(w / 50 / (ms / 10) % 10);
            puts_(" ms per frame\n");
            next = now();
          } else
            solder_key((char)c);
        }
      }
      if ((int32_t)(now() - next) < 0)
        continue;
      /* the model steps that are due: the simulated iron keeps real time
         when a frame takes longer than a step */
      steps = (now() - next) / (SOLDER_TICK_MS * ms) + 1;
      next += steps * SOLDER_TICK_MS * ms;
      ticks += steps;
      send_cnt = send_cycles = 0;
      t0 = now();
      solder_tick(steps);
      t = now() - t0;
      if (send_cnt) {
        frames++;
        total += t;
        waited += send_cycles;
        if (t > worst)
          worst = t;
      }
      /* every 2 s: the frames, draw and send time, stack */
      if (ticks - report >= 20 && frames) {
        report = ticks;
        puts_("frames per second ");
        putdec(frames * 10 / 2 / 10);
        puts_(".");
        putdec(frames * 10 / 2 % 10);
        puts_(", ");
        puts_("frame: ");
        putdec(send_cnt);
        puts_(" requests, draw and send ");
        putdec(total / frames / ms);
        puts_(" ms (max ");
        putdec(worst / ms);
#ifndef USE_CLUT_DMA
        puts_("), cpu busy with the display ");
#else
        puts_("), cpu waits for the display ");
#endif
        putdec(waited / frames / ms);
        puts_(" ms, stack ");
        putdec(stack_used());
        puts_(" bytes\n");
        frames = total = worst = waited = 0;
      }
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
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    graphics_init((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
    for (;;) {
      t0 = now();
      pause = graphics_step();
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
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    clutclock_init((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
    send_cnt = send_cycles = 0;
    next = now();
    for (;;) {
      next += HCLK_HZ;             /* every second, without drift */
      while ((int32_t)(now() - next) < 0)
        ;
      t0 = now();
      clutclock_tick();
      frame_report(now() - t0);
    }
  }
#endif

#ifdef ASYNC_TEST
  {
    /* the speed gauge: the whole screen in one clut_dma request, the cpu
       asleep while it is sent; every 2 s FPS and CPU %, as on the display */
    uint32_t report = now(), fps10, cpu10;

    stack_fill();
    puts_("\nspeed gauge: the whole screen in one clut_dma request, interrupt driven, "
          "the cpu sleeps while it is sent\n");
    lcd_init();
    lcd_cmd1(0x36, ASYNC_MADCTL);  /* MADCTL: landscape 280x240 */
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    async_init((volatile uint8_t *)FBMEM, now);
    for (;;) {
      async_frame();
      if (now() - report >= 2u * HCLK_HZ) {
        report += 2u * HCLK_HZ;
        async_stats(&fps10, &cpu10);
        puts_("fps ");
        putdec(fps10 / 10);
        puts_(".");
        putdec(fps10 % 10);
        puts_(", cpu ");
        putdec(cpu10 / 10);
        puts_(".");
        putdec(cpu10 % 10);
        puts_(" %, stack ");
        putdec(stack_used());
        puts_(" bytes\n");
      }
    }
  }
#endif

#ifdef STARS_TEST
  {
    uint32_t next, t0, t1, draw = 0;
    unsigned frame = 0, late = 0;
    int full = 0;

    puts_("\nfalling stars, Adafruit GFX, 1 bpp, horizontal layout, ");
    putdec(STARS_FPS);
    puts_(" frames per second; every 10 s per star or the whole screen\n");
    lcd_init();
    lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
    stars_init((volatile uint8_t *)FBMEM, send_row);
    send_cnt = send_cycles = 0;
    next = t1 = now();
    for (;;) {
      next += HCLK_HZ / STARS_FPS;
      if ((int32_t)(now() - next) > 0) {
        late++;                    /* the frame before took too long */
        next = now();
      }
      while ((int32_t)(now() - next) < 0)
        ;
      t0 = now();
      stars_tick((volatile uint8_t *)FBMEM, send_row, full);
      draw += now() - t0;
      if (++frame % STARS_FPS == 0) {
        /* per frame: requests, clut_dma cycles, draw and send cycles */
        puts_(full ? "screen: " : "per star: ");
        putdec(STARS_FPS);
        puts_(" frames in ");
        putdec((now() - t1) / (HCLK_HZ / 1000));
        puts_(" ms, ");
        putdec(late);
        puts_(" late; per frame ");
        putdec(send_cnt / STARS_FPS);
        puts_(" requests, clut_dma ");
        putdec(send_cycles / STARS_FPS);
        puts_(" cycles, draw and send ");
        putdec(draw / STARS_FPS);
        puts_(" cycles\n");
        send_cnt = send_cycles = draw = late = 0;
        t1 = now();
        if (frame % (10 * STARS_FPS) == 0)
          full = !full;
      }
    }
  }
#endif

#ifdef PALETTE_TEST
  puts_("\nClutPalette, u8g2\n");
  lcd_init();
  lcd_cmd1(0x3A, 0x03);          /* COLMOD: rgb444 */
  palette((volatile uint8_t *)FBMEM, u8x8_byte_lcd);
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
