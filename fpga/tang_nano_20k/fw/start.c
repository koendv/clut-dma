/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* start.c: vector table and reset handler for the Gowin Cortex-M1 */

#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

int main(void);
void fault(const char *what);
void systick_handler(void);

void reset_handler(void)
{
  uint32_t *s = _sidata, *d = _sdata;

  while (d < _edata)
    *d++ = *s++;
  for (d = _sbss; d < _ebss; d++)
    *d = 0;
  main();
  for (;;)
    ;
}

static void nmi_handler(void)       { fault("nmi"); }
static void hardfault_handler(void) { fault("hard fault"); }
static void default_handler(void)   { fault("unexpected interrupt"); }

/* 16 system exceptions and 32 external interrupts */
__attribute__((section(".vectors"), used))
static void (* const vectors[48])(void) = {
  (void (*)(void))_estack,
  reset_handler,
  nmi_handler,
  hardfault_handler,
  [4 ... 10] = default_handler,
  [11] = default_handler,       /* svcall */
  [12 ... 13] = default_handler,
  [14] = default_handler,       /* pendsv */
  [15] = systick_handler,
  [16 ... 47] = default_handler,
};
