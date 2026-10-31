/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* start.S: startup for the Gowin GoRV32, program loaded into On-Chip RAM
   over JTAG (as BUILD_LOAD of the SDK): no copy of .data. Interrupts off. */

  .section .text.start
  .global _start
_start:
  .option push
  .option norelax
  la    gp, __global_pointer$
  .option pop
  la    sp, _stack_top
  csrw  mie, zero
  csrw  mstatus, zero
  la    t0, trap_entry
  csrw  mtvec, t0

  la    a0, _sbss
  la    a1, _ebss
1:
  bgeu  a0, a1, 2f
  sw    zero, 0(a0)
  addi  a0, a0, 4
  j     1b
2:
  call  main
3:
  j     3b

/* an exception: report mcause and mepc, then stop (main.c) */
  .align 2
trap_entry:
  la    sp, _stack_top
  call  trap_handler
  j     3b
