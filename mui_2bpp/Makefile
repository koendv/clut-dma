# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
#
# mui_2bpp: a minimal u8g2 for a 2 bpp frame buffer, enough for mui, with
# anti-aliased fonts. On the PC, the display is ../fpga/tb/st7789.py: the
# ST7789 of a WeAct 240x280 module, drawn from the bytes sent to it.
#
#   make pattern      the bitmap through the stream function and the ST7789
#                     driver: build/pattern/frame_NN.png, checked against the
#                     formula of clut_dma (demo/check_pattern.py)
#   make font         demo/chars.txt, the characters of the forms, and from it
#                     demo/font_noto_sc_24.c/.h; build/font.png shows the glyphs
#   make demo         the demo menu, a list of keys: build/demo/frame_NN.png
#   make fig          the figures of the READMEs: ../fig/mui2bpp-main.png,
#                     mui2bpp-values.png, mui2bpp-values-zh.png (main_fig.c)
#   make size         the demo for a Cortex-M1: text + data in ITCM
#   make PAGE_ROWS=n  the same with a page buffer of n tile rows (build-pn)
#   make FULL=1 demo  every key the whole screen: the reference for the frames
#                     of a partial update (build-full)
#   make clean        (the generated font in demo/ stays)
#
# Files: src/ the library; u8x8/ copied from u8g2 branch clut, commit
# 07676acf, unchanged; mui/ from the same branch, with MUI_U8G2_GRID and
# MUI_U8G2_2BPP (in ~/src/u8g2 too); tools/ the font tools; demo/ the PC side.

CC     ?= cc
PYTHON ?= python3
ST7789 := ../fpga/tb/st7789.py

B := build$(if $(PAGE_ROWS),-p$(PAGE_ROWS))$(if $(FULL),-full)

# the forms in units of 8x8 tiles: MUI_U8G2_GRID
DEFS   := -DU8X8_WITH_CLUT -DMUI_U8G2_GRID=8 $(if $(PAGE_ROWS),-DU8G2_PAGE_ROWS=$(PAGE_ROWS)) \
          $(if $(FULL),-DMENU_FULL_UPDATES)
CFLAGS := -O2 -g $(DEFS) -Isrc -Iu8x8 -Imui -Idemo -ffunction-sections -fdata-sections
LDFLAGS := -Wl,--gc-sections

SRC  := src/u8g2_2bpp.c src/u8g2_button.c src/u8g2_line.c src/u8g2_circle.c src/u8x8_clut_2bpp.c
U8X8 := u8x8/u8x8_setup.c u8x8/u8x8_display.c u8x8/u8x8_cad.c u8x8/u8x8_byte.c \
        u8x8/u8x8_gpio.c u8x8/u8x8_d_st7789.c u8x8/u8x8_u8toa.c
MUI  := mui/mui.c mui/mui_u8g2.c
LIB  := $(patsubst %.c,$(B)/%.o,$(SRC) $(U8X8) $(MUI))
HDR  := $(wildcard src/*.h u8x8/*.h mui/*.h demo/*.h)

FONT   := font/NotoSansCJKsc-Regular-subset.otf
ICONFONT := font/MaterialSymbolsOutlined-Fill0-opsz20.ttf
FORMS  := demo/forms_en.c demo/forms_zh.c
FONTS  := demo/font_noto_sc_24.c demo/font_icons_20.c demo/font_controls_28.c
DEMO   := demo/main_host.c demo/menu.c demo/host.c $(FONTS) $(FORMS)

# Material Symbols names: the icons of the status row, 20 pixels
ICONS := battery_0_bar battery_1_bar battery_2_bar battery_3_bar battery_4_bar \
         battery_5_bar battery_6_bar battery_full battery_charging_full \
         signal_cellular_alt_1_bar signal_cellular_alt_2_bar signal_cellular_alt \
         network_wifi_1_bar network_wifi_2_bar network_wifi_3_bar wifi bluetooth warning
# and the controls of the forms, 28 pixels
CONTROLS := check_box_outline_blank check_box radio_button_unchecked radio_button_checked

all: pattern demo

# our files with warnings, the copies as they are
$(B)/src/%.o $(B)/demo/%.o: WARN := -Wall -Wextra
$(B)/%.o: %.c $(HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) -c -o $@ $<

$(B)/bin/pattern: $(B)/demo/pattern.o $(B)/demo/host.o $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

pattern: $(B)/bin/pattern
	$(B)/bin/pattern $(B)/pattern.txt $(B)/pattern.bin
	$(PYTHON) $(ST7789) $(B)/pattern.txt $(B)/pattern
	$(PYTHON) demo/check_pattern.py $(B)/pattern.bin $(B)/pattern/final.png

demo/chars.txt: $(FORMS) tools/cstrings.py
	$(PYTHON) tools/cstrings.py -v -o $@ $(FORMS)

# 32 pixel focus bars: ascent 24 + descent 6 + 2; checkboxes 24x24, 3x3 tiles
demo/font_noto_sc_24.c: demo/chars.txt $(FONT) tools/fontconv.py
	@mkdir -p build
	$(PYTHON) tools/fontconv.py -v -s 24 -a 24 -d 6 -m 32-126 -n font_noto_sc_24 \
	  -L "SIL Open Font License 1.1 (font/OFL.txt); the glyphs are a Modified Version of the font" \
	  -o $@ -p build/font.png $(FONT) demo/chars.txt
demo/font_noto_sc_24.h: demo/font_noto_sc_24.c

demo/icons.h: tools/icons.py font/MaterialSymbolsOutlined.codepoints Makefile
	$(PYTHON) tools/icons.py -o $@ font/MaterialSymbolsOutlined.codepoints $(ICONS)

demo/icon_chars.txt: demo/icons.h tools/cstrings.py
	$(PYTHON) tools/cstrings.py -v -o $@ demo/icons.h

# 20 pixel icons: 16 pixels of ink, 18 .. 2 rows above the baseline
demo/font_icons_20.c: demo/icon_chars.txt $(ICONFONT) tools/fontconv.py
	@mkdir -p build
	$(PYTHON) tools/fontconv.py -v -s 20 -a 18 -d 0 -n font_icons_20 \
	  -L "Apache License 2.0 (font/LICENSE-MaterialSymbols.txt)" \
	  -o $@ -p build/font_icons.png $(ICONFONT) demo/icon_chars.txt
demo/font_icons_20.h: demo/font_icons_20.c

demo/controls.h: tools/icons.py font/MaterialSymbolsOutlined.codepoints Makefile
	$(PYTHON) tools/icons.py -o $@ font/MaterialSymbolsOutlined.codepoints $(CONTROLS)

demo/control_chars.txt: demo/controls.h tools/cstrings.py
	$(PYTHON) tools/cstrings.py -v -o $@ demo/controls.h

# 28 pixel controls: a check box of 20x20 pixels of ink, in a field of 3x4 tiles
demo/font_controls_28.c: demo/control_chars.txt $(ICONFONT) tools/fontconv.py
	@mkdir -p build
	$(PYTHON) tools/fontconv.py -v -s 28 -n font_controls_28 \
	  -L "Apache License 2.0 (font/LICENSE-MaterialSymbols.txt)" \
	  -o $@ -p build/font_controls.png $(ICONFONT) demo/control_chars.txt
demo/font_controls_28.h: demo/font_controls_28.c

font: $(FONTS)

$(B)/demo/menu.o: demo/font_noto_sc_24.h demo/font_icons_20.h demo/icons.h demo/font_controls_28.h demo/controls.h

$(B)/bin/demo: $(patsubst %.c,$(B)/%.o,$(DEMO)) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

demo: $(B)/bin/demo
	$(B)/bin/demo $(B)/demo.txt $(B)/demo-names.txt
	$(PYTHON) $(ST7789) $(B)/demo.txt $(B)/demo $(B)/demo-names.txt

# the figures of the READMEs, generated: frames of the menu (demo/main_fig.c)
FIG := $(patsubst %.c,$(B)/%.o,demo/main_fig.c demo/menu.c demo/host.c $(FONTS) $(FORMS)) $(LIB)
$(B)/demo/main_fig.o: demo/font_noto_sc_24.h

$(B)/bin/fig: $(FIG)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

fig: $(B)/bin/fig
	$(B)/bin/fig $(B)/fig.txt
	$(PYTHON) $(ST7789) $(B)/fig.txt $(B)/fig
	cp $(B)/fig/frame_00.png ../fig/mui2bpp-main.png
	cp $(B)/fig/frame_01.png ../fig/mui2bpp-values.png
	cp $(B)/fig/frame_02.png ../fig/mui2bpp-values-zh.png

# Cortex-M1: the library, mui, the menu and the font, with stubs for the board.
# text + data go to ITCM; bss to DTCM, with the buffer (fbmem on the board)
ARMCC   ?= arm-none-eabi-gcc
ARMSIZE ?= arm-none-eabi-size
ARMSRC  := demo/main_size.c demo/menu.c $(FONTS) $(FORMS) $(SRC) $(U8X8) $(MUI)

$(B)/bin/size.elf: $(ARMSRC) $(HDR) demo/font_noto_sc_24.h demo/font_icons_20.h demo/icons.h demo/font_controls_28.h demo/controls.h
	@mkdir -p $(dir $@)
	$(ARMCC) -mcpu=cortex-m1 -mthumb -Os $(DEFS) -Isrc -Iu8x8 -Imui -Idemo -ffunction-sections -fdata-sections \
	  --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=$(B)/bin/size.map \
	  -o $@ $(ARMSRC)

size: $(B)/bin/size.elf
	$(ARMSIZE) $<

clean:
	rm -rf build build-p* build-full

.PHONY: all pattern font demo fig size clean
