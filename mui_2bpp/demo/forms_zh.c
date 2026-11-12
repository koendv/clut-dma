/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  forms_zh.c: the menus of the demo in Chinese (simplified), the same forms
  and fields as forms_en.c, in units of 8x8 tiles.
*/

#include "menu.h"

fds_t fds_zh[] =

MUI_FORM(1)
MUI_STYLE(0)
MUI_LABEL(1, 5, "主菜单")
MUI_DATA("GP",
    MUI_10 "设置|"
    MUI_20 "数值|"
    MUI_30 "关于")
MUI_XYA("GC", 1, 9, 0)
MUI_XYA("GC", 1, 13, 1)
MUI_XYA("GC", 1, 17, 2)

MUI_FORM(10)
MUI_STYLE(0)
MUI_LABEL(1, 5, "设置")
MUI_LABEL(1, 9, "语言")
MUI_XYAT("LA", 17, 9, 96, "English|中文")
MUI_LABEL(1, 13, "背光")
MUI_XY("C1", 25, 13)
MUI_LABEL(1, 17, "休眠")
MUI_XY("C2", 25, 17)
MUI_XYT("BK", 1, 29, "返回")

MUI_FORM(20)
MUI_STYLE(0)
MUI_LABEL(1, 5, "数值")
MUI_LABEL(1, 9, "亮度")
MUI_XY("BR", 19, 9)
MUI_LABEL(1, 13, "波特率")
MUI_XYAT("SP", 16, 13, 96, "9600|19200|38400|57600|115200|230400|460800|921600")
MUI_LABEL(1, 17, "停止位")
MUI_XYAT("RP", 16, 17, 0, "1")
MUI_XYAT("RP", 23, 17, 1, "2")
MUI_XYT("BK", 1, 29, "返回")

MUI_FORM(30)
MUI_STYLE(0)
MUI_LABEL(1, 5, "关于")
MUI_XYA("AB", 1, 9, 0)
MUI_XYAT("AB", 1, 13, 1, "显存")
MUI_XYAT("AB", 1, 17, 2, "程序")
MUI_XYAT("AB", 1, 21, 3, "响应")
MUI_XYAT("AB", 1, 25, 4, "全屏")
MUI_XYAT("BF", 15, 29, 1, "主菜单")

;
