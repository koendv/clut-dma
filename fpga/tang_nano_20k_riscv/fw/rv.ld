/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* Gowin GoRV32: code and data in On-Chip RAM, loaded over JTAG. */

MEMORY
{
  RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 32K
}

ENTRY(_start)

SECTIONS
{
  .text :
  {
    KEEP(*(.text.start))
    *(.text*)
    *(.rodata*)
    *(.srodata*)
    . = ALIGN(4);
  } > RAM

  .data :
  {
    *(.data*)
    . = ALIGN(4);
    __global_pointer$ = . + 0x800;
    *(.sdata*)
    . = ALIGN(4);
  } > RAM

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.sbss*)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  /* stack: the rest of the RAM, at least 1 KiB. highest use measured:
     664 bytes (make solder SOFT=1, with the menu) */
  ASSERT(_ebss + 1024 <= _stack_top, "On-Chip RAM: less than 1 KiB left for the stack")
}
