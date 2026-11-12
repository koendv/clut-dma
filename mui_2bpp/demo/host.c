/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  host.c: the display on the PC. The u8x8 byte callback writes what goes
  over SPI to a text file, as ../fpga/tb/st7789.py reads it: "c xx" a
  command byte, "d xx" a data byte, "f" the end of a frame.
  host_frame() also counts the RAMWR commands since the last frame.
*/

#include <stdio.h>
#include <stdlib.h>
#include "host.h"

static FILE *spi;
static uint8_t dc;
static unsigned ramwr;          /* RAMWR commands: a tile row each */

void host_open(const char *path)
{
  spi = fopen(path, "w");
  if ( spi == NULL )
  {
    perror(path);
    exit(1);
  }
}

void host_close(void)
{
  fclose(spi);
}

unsigned host_frame(void)
{
  unsigned n = ramwr;

  fprintf(spi, "f\n");
  ramwr = 0;
  return n;
}

uint8_t host_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  uint8_t *data = (uint8_t *)arg_ptr;

  (void)u8x8;
  switch ( msg )
  {
    case U8X8_MSG_BYTE_SEND:
      while ( arg_int-- > 0 )
      {
        if ( !dc && *data == 0x2C )
          ramwr++;
        fprintf(spi, "%c %02x\n", dc ? 'd' : 'c', *data++);
      }
      break;
    case U8X8_MSG_BYTE_SET_DC:
      dc = arg_int;
      break;
    default:
      break;
  }
  return 1;
}

uint8_t host_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}
