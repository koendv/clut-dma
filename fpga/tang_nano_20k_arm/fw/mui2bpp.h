/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  mui2bpp.h: the menu of ../../../mui_2bpp on the Tang Nano 20K: where the
  library keeps its buffer, tile map and clut in fbmem, the memory clut_dma
  reads. The same for the firmware (mui2bpp.c) and the simulation
  (sim_mui2bpp.c).
*/

#ifndef MUI2BPP_H
#define MUI2BPP_H

#include "u8g2.h"                 /* of mui_2bpp: U8G2_BUF_SIZE, U8G2_MAP_SIZE */

#define MUI2_CLUT_OFS 0x0000u     /* 16 entries of 2 words, 128-byte aligned */
#define MUI2_MAP_OFS  0x0080u     /* U8G2_MAP_SIZE bytes */
#define MUI2_FB_OFS   ((MUI2_MAP_OFS + U8G2_MAP_SIZE + 7u) & ~7u)
#define MUI2_FBMEM_USED (MUI2_FB_OFS + U8G2_BUF_SIZE)

#if MUI2_FBMEM_USED > 16384
#error "mui2bpp: buffer, map and clut larger than fbmem: make PAGE_ROWS=..."
#endif

#endif
