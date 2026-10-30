/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* Gowin Cortex-M1: code in ITCM, data in DTCM. .data is copied from ITCM. */

MEMORY
{
  ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 32K
  DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(reset_handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > ITCM

  .data :
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT > ITCM
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > DTCM

  _estack = ORIGIN(DTCM) + LENGTH(DTCM);
}
