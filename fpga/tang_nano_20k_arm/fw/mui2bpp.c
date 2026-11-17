/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  mui2bpp.c: the menu of ../../../mui_2bpp on the Tang Nano 20K, with the
  WeAct ST7789 240x280: mui, English and Chinese, 2 bits per pixel,
  anti-aliased; a 240x240 screen at the top of the panel.

  The u8x8 ST7789 driver sends a tile row at a time: the window commands,
  then the pixels from u8x8_clut_stream_rgb444(). With USE_CLUT_DMA that is
  u8x8_clut_dma_2bpp.c, a request to clut_dma with CTRL BPP2; without it
  mui_2bpp/src/u8x8_clut_2bpp.c, the cpu expands the colors. The buffer,
  the tile map and the clut are in fbmem (mui2bpp.h), as clut_dma needs.

  Keys on UART0 (make term): w s or up down: field, space enter: select,
  i: other status icons, b: benchmark, 20 screens.

  make mui2bpp STATS=1 (MUI2_STATS): after each key the tile rows sent and
  the time of the update, e.g. "s: 8 tile rows, 10512 us"; key f: a
  benchmark, 20 focus changes, Next and Prev in turn (from the first field
  of a form: no wrap). The about form shows the last partial update (a
  focus change, a select in a field) and the last whole screen, measured.

  The about form: the cpu and its clock, the memory for the display, and
  the code: text and data, from the symbols of the linker script.

  Also for the GoRV32 build (fpga/tang_nano_20k_riscv/fw, __riscv): UART1,
  mcycle as the clock, and the buffer, map and clut in On-Chip RAM, which
  clut_dma reads over AHB; a page buffer (U8G2_PAGE_ROWS) to fit in 32 KiB.
*/

#include <stdint.h>
#include <hw.h>                  /* the board's: -I. first, not this directory */
#include "menu.h"
#ifndef __riscv
#include "mui2bpp.h"              /* fbmem: the buffer, map and clut */
#endif

#ifndef LCD_DIV
#define LCD_DIV    1u             /* sck = 54 MHz / 2 = 27 MHz, the fastest */
#endif
#ifndef LCD_BRIGHT
#define LCD_BRIGHT 128u           /* backlight 128/256, 50% */
#endif
#define BL (SPI_BL | (LCD_BRIGHT << 8))
#define ON (SPI_RST_N | BL)       /* out of reset, backlight on */

/*=== uart ===*/

#ifdef __riscv
static void uart_init(void)
{
  uint32_t div = (HCLK_HZ + 8u * 115200) / (16u * 115200);

  UART1_LCR = 0x83;              /* DLAB, 8 bits, no parity, 1 stop bit */
  UART1_DLL = div & 0xFF;
  UART1_DLM = div >> 8;
  UART1_LCR = 0x03;
  UART1_FCR = 0x07;              /* fifos on and cleared */
}

/* a received character, or -1 */
static int getch(void)
{
  return (UART1_LSR & 1) ? (int)(UART1_RBR & 0xFF) : -1;
}

static void putch(char c)
{
  while (!(UART1_LSR & 0x20))
    ;
  UART1_THR = (uint8_t)c;
}
#else
static void uart_init(void)
{
  UART0_BAUDDIV = (HCLK_HZ + 115200 / 2) / 115200;
  UART0_CTRL = 3;                /* tx and rx */
}

/* a received character, or -1 */
static int getch(void)
{
  return (UART0_STATE & 2) ? (int)(UART0_DATA & 0xFF) : -1;
}

static void putch(char c)
{
  while (UART0_STATE & 1)
    ;
  UART0_DATA = (uint8_t)c;
}
#endif

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

void fault(const char *what)
{
  puts_("\n*** ");
  puts_(what);
  puts_("\n");
  for (;;)
    ;
}

#ifdef __riscv
/*=== time: mcycle, the cycle counter, at hclk ===*/

#define CSR_READ(r) ({ uint32_t v_; __asm volatile ("csrr %0, " #r : "=r"(v_)); v_; })

/* an exception, from start.S */
void trap_handler(void);
void trap_handler(void)
{
  puts_("\n*** trap, mcause ");
  putdec(CSR_READ(mcause));
  puts_(" mepc ");
  putdec(CSR_READ(mepc));
  fault("stop");
}

static void time_init(void)
{
}

static uint32_t now(void)
{
  return CSR_READ(mcycle);
}
#else
/*=== time: SysTick at hclk, 24 bits, extended by the wrap interrupt ===*/

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
#endif

static void delay_ms(uint32_t ms)
{
  uint32_t t0 = now();

  while (now() - t0 < ms * (HCLK_HZ / 1000))
    ;
}

/*=== the display: u8x8 byte and gpio functions on spi_lcd ===*/

static void spi_wait(void)
{
  while (SPI_STATUS & 1)
    ;
}

#ifdef MUI2_STATS
static uint8_t lcd_dc;           /* 1: data */
static uint32_t ramwr;           /* RAMWR commands: a tile row each */
#endif

/* chip select low for a transfer, high between them: the display stays
   black if chip select is low all the time */
static uint8_t byte_lcd(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  const uint8_t *p = (const uint8_t *)arg_ptr;

  (void)u8x8;
  switch (msg) {
  case U8X8_MSG_BYTE_SEND:
    while (arg_int--) {
#ifdef MUI2_STATS
      if (!lcd_dc && *p == 0x2C)
        ramwr++;
#endif
      SPI_DATA = *p++;           /* PREADY low while the data register is full */
    }
    break;
  case U8X8_MSG_BYTE_SET_DC:
#ifdef MUI2_STATS
    lcd_dc = arg_int;
#endif
    spi_wait();
    SPI_CTRL = ON | (arg_int ? SPI_DC : 0u);
    break;
  case U8X8_MSG_BYTE_START_TRANSFER:
    spi_wait();
    SPI_CTRL = ON | SPI_CS_N;
    SPI_CTRL = ON;               /* chip select low */
    break;
  case U8X8_MSG_BYTE_END_TRANSFER:
    spi_wait();
    SPI_CTRL = ON | SPI_CS_N;    /* chip select high */
    break;
  default:
    break;
  }
  return 1;
}

static uint8_t gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8;
  (void)arg_ptr;
  switch (msg) {
  case U8X8_MSG_DELAY_MILLI:
    delay_ms(arg_int);
    break;
  case U8X8_MSG_GPIO_RESET:
    spi_wait();
    SPI_CTRL = (arg_int ? ON : BL) | SPI_CS_N;
    break;
  default:
    break;
  }
  return 1;
}

/*=== the menu ===*/

static const menu_status_t status_cycle[] = {
  { 5, 3, 3, 1, 0 },
  { 1, 2, 0, 1, 1 },
  { 8, 2, 2, 0, 0 },
  { 7, 3, 4, 1, 0 },
};

static void benchmark(u8g2_t *u8g2)
{
  uint32_t t0 = now(), cycles;
  unsigned i;

  for (i = 0; i < 20; i++)
    menu_draw(u8g2);
  cycles = (now() - t0) / 20;
#ifdef MUI2_STATS
  menu_screen_us = cycles / (HCLK_HZ / 1000000u);
#endif
  puts_("a screen, drawn and sent: ");
  putdec(cycles / (HCLK_HZ / 1000000u));
  puts_(" us\n");
}

#ifdef MUI2_STATS
/* the time of an update for the about form: fewer tile rows than the
   screen, a partial update (a focus change, a select in a field); else a
   whole screen */
static void record(uint32_t cycles)
{
  uint32_t us = cycles / (HCLK_HZ / 1000000u);

  if (ramwr < U8G2_TILE_ROWS)
    menu_response_us = us;
  else
    menu_screen_us = us;
}

/* the tile rows sent and the time of an update */
static void stats(const char *key, uint32_t cycles)
{
  puts_(key);
  puts_(": ");
  putdec(ramwr);
  puts_(" tile rows, ");
  putdec(cycles / (HCLK_HZ / 1000000u));
  puts_(" us\n");
}

/* 20 focus changes, Next and Prev in turn */
static void benchmark_focus(u8g2_t *u8g2)
{
  uint32_t t0, cycles;
  unsigned i;

  ramwr = 0;
  t0 = now();
  for (i = 0; i < 20; i++) {
    menu_event((i & 1) ? MENU_PREV : MENU_NEXT);
    menu_draw(u8g2);
  }
  cycles = (now() - t0) / 20;
  ramwr /= 20;
  record(cycles);
  stats("a focus change, drawn and sent", cycles);
}
#endif

/* the image: text and data (rv.ld, m1.ld) */
#ifdef __riscv
extern char _start[], _sbss[];
#define CPU_NAME "RISC-V"
#define CODE_BYTES ((uint32_t)(_sbss - _start))
#else
extern char _sidata[], _sdata[], _edata[];
#define CPU_NAME "Cortex-M1"
#define CODE_BYTES ((uint32_t)_sidata + (uint32_t)(_edata - _sdata))   /* ITCM from 0 */
#endif

#ifdef __riscv
/* in On-Chip RAM; clut_dma reads them over AHB */
static uint8_t buf[U8G2_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t map[U8G2_MAP_SIZE];
static uint32_t clut[16][2] __attribute__((aligned(128)));
#endif

int main(void)
{
  u8g2_t u8g2;
#ifndef __riscv
  uint8_t *fbmem = (uint8_t *)FBMEM;
  uint8_t *buf = fbmem + MUI2_FB_OFS, *map = fbmem + MUI2_MAP_OFS;
  uint32_t (*clut)[2] = (uint32_t (*)[2])(void *)(fbmem + MUI2_CLUT_OFS);
#endif
  unsigned s = 0;
  int c, esc = 0;
#ifdef MUI2_STATS
  uint32_t t0;
  const char *key;
#endif

  uart_init();
  time_init();
  GPIO = GPIO_LED;
  SPI_DIV = LCD_DIV;
#ifdef USE_CLUT_DMA
  puts_("\nmui_2bpp, clut_dma: ");
#else
  puts_("\nmui_2bpp, the cpu expands the colors: ");
#endif
  puts_("w s or up down: field, space enter: select, i: status, b: benchmark\n");
#ifdef MUI2_STATS
  puts_("stats: tile rows and time per key, f: benchmark of a focus change\n");
#endif
#ifdef USE_CLUT_DMA
  DMA_CTRL = DMA_BPP2;           /* not started: CTRL reads back what the bitstream has */
  if (!(DMA_CTRL & DMA_BPP2))
    puts_("error: CTRL BPP2 reads 0: a bitstream without BPP2\n");
#endif

  u8g2_Setup_st7789_240x280(&u8g2, buf, byte_lcd, gpio_and_delay);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFlipMode(&u8g2, 1);    /* the connector at the top, as LCD_MADCTL 0x00 */
  menu_init(&u8g2, map, clut);
  menu_cpu_name = CPU_NAME;
  menu_cpu_mhz = HCLK_HZ / 1000000u;
  menu_code_bytes = CODE_BYTES;
  u8g2_ClearDisplay(&u8g2);
#ifdef MUI2_STATS
  ramwr = 0;
  t0 = now();
  menu_draw(&u8g2);
  record(now() - t0);
#else
  menu_draw(&u8g2);
#endif

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
      c = c == 'A' ? 'w' : c == 'B' ? 's' : 0;
    }
    switch (c) {
    case 'w':
      menu_event(MENU_PREV);
      break;
    case 's':
      menu_event(MENU_NEXT);
      break;
    case ' ':
    case '\r':
    case '\n':
      menu_event(MENU_SELECT);
      break;
    case 'i':
      s = (s + 1) % (sizeof status_cycle / sizeof status_cycle[0]);
#ifdef MUI2_STATS
      ramwr = 0;
      t0 = now();
      menu_status(&u8g2, &status_cycle[s]);
      stats("i", now() - t0);     /* the status row: not on the about form */
#else
      menu_status(&u8g2, &status_cycle[s]);
#endif
      continue;
    case 'b':
      benchmark(&u8g2);
      continue;
#ifdef MUI2_STATS
    case 'f':
      benchmark_focus(&u8g2);
      continue;
#endif
    default:
      continue;
    }
#ifdef MUI2_STATS
    key = c == 'w' ? "w" : c == 's' ? "s" : "select";
    ramwr = 0;
    t0 = now();
    menu_draw(&u8g2);
    t0 = now() - t0;
    record(t0);
    stats(key, t0);
#else
    menu_draw(&u8g2);
#endif
  }
}
