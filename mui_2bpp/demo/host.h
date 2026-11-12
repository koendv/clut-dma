/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

#ifndef HOST_H
#define HOST_H

#include "u8g2.h"

void host_open(const char *path);   /* the spi text file */
void host_close(void);
unsigned host_frame(void);          /* the end of a frame: the RAMWR commands sent */
uint8_t host_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t host_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif
