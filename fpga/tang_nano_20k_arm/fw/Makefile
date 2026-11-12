# firmware for the Gowin Cortex-M1 on the Tang Nano 20K
#   make         build fw.elf
#   make bit     load the bitstream into the fpga (sram)
#   make load    load fw.elf into ITCM over SWD and run it
#   make term    serial terminal on UART0, 115200 baud
#   make scl     test: SCL toggles without pause, 50% duty, for a multimeter
#   make lcd     test: the display without clut_dma, colors in a loop
#   make border  test: a colored line on each border, white block at (0, 0)
#   make palette u8g2: ClutPalette of the u8g2 clut branch, through clut_dma
#   make clutcolor  u8g2: ClutColor, a dashboard updated every second
#   make clutclock  u8g2: ClutClock, an analog clock with a color cycling ring
#   make graphics   u8g2: demo_graphics, the GraphicsTest pages
#   make mui        u8g2: demo_mui, a menu, keys on UART0 (make term)
#   make stars      Adafruit GFX: falling stars, 1 bpp, horizontal layout
#   make solder     soldering station, 24x24 Chinese (solder.c), a page buffer
#                   of PAGE_ROWS tile rows (default 2), black and white.
#                   COLOR=1: a tile map and a clut. USE_CLUT_DMA=0: the cpu
#                   expands the colors, no clut_dma. key b: a benchmark
#   make async   a speed gauge: the whole screen in one clut_dma request,
#                interrupt driven, the cpu asleep while it is sent (async.c)
#   make demo-solder  solder with keys over the uart, for a video
#                   (solder_demo.sh). OPTS: the build options of make solder,
#                   default COLOR=1; OPTS= for black and white
#   make demo-menu  the anti-aliased menu with keys over the uart, for a
#                   video (mui2bpp_demo.sh). MENU_OPTS: the build options of
#                   make mui2bpp, default STATS=1
#   make mui2bpp    mui_2bpp: the menu of ../../../mui_2bpp, 2 bits per
#                   pixel, anti-aliased, English and Chinese, keys on UART0
#                   (mui2bpp.c). USE_CLUT_DMA=0: the cpu expands the colors.
#                   key b: a benchmark. STATS=1: tile rows and time per
#                   key, key f: a benchmark of a focus change
#   make sim-mui2bpp  mui_2bpp in the simulation, the cpu expanding the colors
#                   and clut_dma: sim-mui2bpp/{cpu,dma}/frame_NN.png, and the
#                   bytes sent to the display compared, they must be the same
#   make sim     the image the display ought to show, from a simulation of the
#                RTL: sim/frame_NN.png per request, sim/final.png.
#                make sim TEST=border, palette, clutcolor, clutclock, graphics, mui, stars
#
# U8G2: a u8g2 tree with the clut branch, https://github.com/koendv/u8g2
# ADAFRUIT_GFX: Adafruit GFX Library, https://github.com/adafruit/Adafruit-GFX-Library
#   (1.13.0), only for make stars; the Arduino headers it needs are in arduino/

CROSS   ?= /opt/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin/arm-none-eabi-
OPENOCD ?= /opt/xpack-openocd-0.12.0-4/bin/openocd
TTY     ?= /dev/ttyUSB1
U8G2    ?= ../../../../u8g2
ADAFRUIT_GFX ?= $(HOME)/Arduino/libraries/Adafruit_GFX_Library

CC      = $(CROSS)gcc
CXX     = $(CROSS)g++
AR      = $(CROSS)ar
CFLAGS  = -mcpu=cortex-m1 -mthumb -Os -g -Wall -Wextra -ffreestanding \
	-ffunction-sections -fdata-sections -I.
# spi clock: make -B LCD_DIV=64 load
ifdef LCD_DIV
CFLAGS += -DLCD_DIV=$(LCD_DIV)u
endif
# backlight: make -B LCD_BRIGHT=128 load, BRIGHT/256 of the time on (0: always)
ifdef LCD_BRIGHT
CFLAGS += -DLCD_BRIGHT=$(LCD_BRIGHT)u
endif
# the demos that send through u8g2 (clutclock graphics mui solder): clut_dma
# sends the pixels, u8x8_clut_dma.c in place of the software of u8g2;
# USE_CLUT_DMA=0: the cpu expands the colors
USE_CLUT_DMA ?= 1
ifeq ($(USE_CLUT_DMA),1)
CFLAGS += -DUSE_CLUT_DMA
SIMDEFS += -DUSE_CLUT_DMA
endif
ifdef COLOR
CFLAGS += -DSOLDER_COLOR
endif
# make mui2bpp STATS=1: tile rows and time per key, key f (mui2bpp.c)
ifdef STATS
CFLAGS += -DMUI2_STATS
endif
# u8g2 page buffer of ClutClock, demo_graphics and demo_mui: make PAGE_ROWS=1 clutclock,
# 1 or 2 tile rows; default the full screen
ifdef PAGE_ROWS
CFLAGS += -DPAGE_ROWS=$(PAGE_ROWS)
SIMDEFS += -DPAGE_ROWS=$(PAGE_ROWS)
endif
# orientation: make -B LCD_MADCTL=0xC0 load, for the connector at the bottom
ifdef LCD_MADCTL
CFLAGS += -DLCD_MADCTL=$(LCD_MADCTL)u
SIMDEFS += -DLCD_MADCTL=$(LCD_MADCTL)u
endif
CXXFLAGS = $(filter-out -ffreestanding,$(CFLAGS)) -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-use-cxa-atexit
GFX_DEFS = -DARDUINO=100 -Iarduino -I$(ADAFRUIT_GFX)
GFX_HDR  = arduino/Arduino.h arduino/Print.h testimg.h
LDFLAGS = -nostartfiles --specs=nano.specs -T m1.ld -Wl,--gc-sections -Wl,-Map=$(@:.elf=.map)

# u8g2 as a library, for the cpu and for the simulation on the host.
# u8x8_d_gc9a01.c needs the arduino pin list; it is not used here.
# mui with MUI_U8G2_HIRES: the display is 280 pixels high
U8G2_DEFS = -DU8X8_WITH_CLUT -DMUI_U8G2_HIRES -I$(U8G2)/csrc
U8G2_SRC  = $(abspath $(filter-out %/u8x8_d_gc9a01.c,$(wildcard $(U8G2)/csrc/*.c)))

lib-m1/libu8g2.a: $(U8G2_SRC)
	mkdir -p lib-m1
	cd lib-m1 && for f in $(U8G2_SRC); do $(CC) $(CFLAGS) $(U8G2_DEFS) -w -c $$f || exit 1; done
	$(AR) rcs $@ lib-m1/*.o

lib-host/libu8g2.a: $(U8G2_SRC)
	mkdir -p lib-host
	cd lib-host && for f in $(U8G2_SRC); do $(HOSTCC) -O2 $(U8G2_DEFS) -w -c $$f || exit 1; done
	ar rcs $@ lib-host/*.o

# u8x8_clut_dma.c an object on the command line, not in a library: it
# replaces a weak function of libu8g2.a
FW_SRC = start.c main.c testimg.c u8x8_clut_dma.c
FW_DEP = $(FW_SRC) testimg.h hw.h m1.ld lib-m1/libu8g2.a

fw.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -o $@ $(FW_SRC) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# test variants: make scl, make lcd, make border, make palette
scl.elf lcd.elf border.elf palette.elf clutcolor.elf clutclock.elf graphics.elf mui.elf: %.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -D$(shell echo $* | tr a-z A-Z)_TEST -o $@ $(FW_SRC) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc

# Adafruit GFX and the demo, c++, not in the u8g2 libraries
obj-m1/Adafruit_GFX.o: $(ADAFRUIT_GFX)/Adafruit_GFX.cpp $(GFX_HDR)
	mkdir -p obj-m1
	$(CXX) $(CXXFLAGS) $(GFX_DEFS) -w -c -o $@ $<

obj-m1/stars.o: stars.cpp $(GFX_HDR)
	mkdir -p obj-m1
	$(CXX) $(CXXFLAGS) $(GFX_DEFS) -c -o $@ $<

STARS_OBJ = obj-m1/stars.o obj-m1/Adafruit_GFX.o

stars.elf: $(FW_DEP) $(STARS_OBJ)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DSTARS_TEST -o $@ $(FW_SRC) $(STARS_OBJ) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# the soldering station demo: 24x24 Chinese, a page buffer, black and white
SOLDER_SRC = solder.c font/u8g2_font_gb24st_t_solder.c

solder.elf: $(FW_DEP) $(SOLDER_SRC)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DSOLDER_TEST -o $@ $(FW_SRC) $(SOLDER_SRC) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# the speed gauge: one request per screen, interrupt driven
async.elf: $(FW_DEP) async.c
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DASYNC_TEST -o $@ $(FW_SRC) async.c $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc
	$(CROSS)size $@

scl lcd border palette clutcolor clutclock graphics mui stars solder async: %: %.elf
	$(OPENOCD) -f ../m1.cfg -c "init; m1_run $<; shutdown"

# mui_2bpp: its own u8g2.h, not the u8g2 of the other demos. The copies of
# u8x8 and mui without warnings, as libu8g2.a; the pixels from clut_dma
# (u8x8_clut_dma_2bpp.c) or the cpu (mui_2bpp/src/u8x8_clut_2bpp.c)
MUI2      = ../../../mui_2bpp
MUI2_DEFS = -DU8X8_WITH_CLUT -DMUI_U8G2_GRID=8 -I$(MUI2)/src -I$(MUI2)/u8x8 -I$(MUI2)/mui -I$(MUI2)/demo \
	$(if $(PAGE_ROWS),-DU8G2_PAGE_ROWS=$(PAGE_ROWS))
MUI2_COPY = $(addprefix $(MUI2)/,u8x8/u8x8_setup.c u8x8/u8x8_display.c u8x8/u8x8_cad.c \
	u8x8/u8x8_byte.c u8x8/u8x8_gpio.c u8x8/u8x8_d_st7789.c u8x8/u8x8_u8toa.c mui/mui.c mui/mui_u8g2.c)
MUI2_SRC  = $(addprefix $(MUI2)/,src/u8g2_2bpp.c src/u8g2_button.c src/u8g2_circle.c demo/menu.c demo/forms_en.c \
	demo/forms_zh.c demo/font_noto_sc_24.c demo/font_icons_20.c demo/font_controls_28.c)
MUI2_HDR  = $(wildcard $(MUI2)/src/*.h $(MUI2)/u8x8/*.h $(MUI2)/mui/*.h $(MUI2)/demo/*.h) mui2bpp.h hw.h
MUI2_CPU  = $(MUI2)/src/u8x8_clut_2bpp.c

mui2bpp.elf: start.c mui2bpp.c u8x8_clut_dma_2bpp.c $(MUI2_SRC) $(MUI2_COPY) $(MUI2_CPU) $(MUI2_HDR) m1.ld
	rm -rf obj-mui2bpp && mkdir -p obj-mui2bpp
	cd obj-mui2bpp && $(CC) $(filter-out -Wall -Wextra,$(CFLAGS)) -w $(MUI2_DEFS:-I../%=-I../../%) \
	  -c $(addprefix ../,$(MUI2_COPY))
	$(CC) $(CFLAGS) $(MUI2_DEFS) -o $@ start.c mui2bpp.c \
	  $(if $(filter 1,$(USE_CLUT_DMA)),u8x8_clut_dma_2bpp.c,$(MUI2_CPU)) $(MUI2_SRC) \
	  obj-mui2bpp/*.o $(LDFLAGS) -lc -lgcc
	$(CROSS)size $@

mui2bpp: mui2bpp.elf
	$(OPENOCD) -f ../m1.cfg -c "init; m1_run $<; shutdown"

# mui_2bpp in the simulation, twice: the cpu expands the colors (d steps) and
# clut_dma (r steps, BPP2); the bytes to the display must be the same
sim-mui2bpp: sim_mui2bpp.c $(MUI2_SRC) $(MUI2_COPY) $(MUI2_CPU) $(MUI2_HDR) ../../tb/tb_frame.v ../../tb/st7789.py
	for v in cpu dma; do \
	  rm -rf sim-mui2bpp/$$v && mkdir -p sim-mui2bpp/$$v && \
	  $(HOSTCC) -O2 $(MUI2_DEFS) -I. $$(test $$v = dma && echo -DUSE_CLUT_DMA) -o sim-mui2bpp/$$v/host \
	    sim_mui2bpp.c $(MUI2_SRC) $(MUI2_COPY) $$(test $$v = cpu && echo $(MUI2_CPU)) && \
	  (cd sim-mui2bpp/$$v && ./host && \
	    $(IVERILOG) -g2005 -Wall -o tb_frame ../../../../tb/tb_frame.v $(addprefix ../,$(SIMRTL)) && \
	    $(VVP) -n tb_frame > vvp.log && \
	    python3 ../../../../tb/st7789.py spi.txt . names.txt) || exit 1; \
	done
	cmp sim-mui2bpp/cpu/spi.txt sim-mui2bpp/dma/spi.txt && \
	  echo "sim-mui2bpp: clut_dma sends the same bytes as the cpu"

# simulation: host.c writes fbmem and the steps of the test, tb_frame.v runs
# them through clut_fabric, st7789.py draws the spi bytes
HOSTCC   ?= cc
IVERILOG ?= iverilog
VVP      ?= vvp
HOSTCXX  ?= c++
HOSTCXXFLAGS = -O2 -Wall -Wextra -std=gnu++17 -fno-exceptions -fno-rtti

# make sim TEST=stars: with Adafruit GFX
ifeq ($(TEST),stars)
SIMDEFS += -DWITH_STARS
SIMOBJ   = obj-host/stars.o obj-host/Adafruit_GFX.o
endif

# make sim TEST=solder: the soldering station demo, its own fbmem layout
ifeq ($(TEST),solder)
SIMDEFS += -DSOLDER_TEST $(filter -DSOLDER_% -DPAGE_ROWS=%,$(CFLAGS))
SIMSRC   = solder.c font/u8g2_font_gb24st_t_solder.c
endif


obj-host/Adafruit_GFX.o: $(ADAFRUIT_GFX)/Adafruit_GFX.cpp $(GFX_HDR)
	mkdir -p obj-host
	$(HOSTCXX) $(HOSTCXXFLAGS) $(GFX_DEFS) -w -c -o $@ $<

obj-host/stars.o: stars.cpp $(GFX_HDR)
	mkdir -p obj-host
	$(HOSTCXX) $(HOSTCXXFLAGS) $(GFX_DEFS) -c -o $@ $<

SIMRTL    = ../../../../rtl/clut_dma.v ../../rtl/clut_fabric.v ../../rtl/apb_reg_slave.v \
	../../../rtl/spi_lcd.v ../../../rtl/fbmem.v

sim: host.c testimg.c testimg.h solder.c font/u8g2_font_gb24st_t_solder.c ../../tb/tb_frame.v ../../tb/st7789.py lib-host/libu8g2.a $(SIMOBJ)
	mkdir -p sim
	$(HOSTCC) -O2 -Wall -Wextra $(SIMDEFS) $(U8G2_DEFS) -o sim/host host.c testimg.c $(SIMSRC) $(SIMOBJ) lib-host/libu8g2.a -lm
	cd sim && ./host $(TEST) && \
	  $(IVERILOG) -g2005 -Wall -o tb_frame ../../../tb/tb_frame.v $(SIMRTL) && \
	  $(VVP) -n tb_frame && \
	  python3 ../../../tb/st7789.py spi.txt . names.txt

bit:
	$(MAKE) -C .. load

load: fw.elf
	$(OPENOCD) -f ../m1.cfg -c "init; m1_run fw.elf; shutdown"

# the examples one after the other and a walk through the menu, for a video
demo:
	./demo.sh

# the soldering station with keys over the uart, for a video
OPTS ?= COLOR=1
demo-solder:
	OPTS='$(OPTS)' TTY='$(TTY)' ./solder_demo.sh

# the anti-aliased menu with keys over the uart, for a video
MENU_OPTS ?= STATS=1
demo-menu:
	OPTS='$(MENU_OPTS)' TTY='$(TTY)' ./mui2bpp_demo.sh

term:
	minicom -D $(TTY) -b 115200

clean:
	rm -f *.elf *.map
	rm -rf sim

distclean: clean
	rm -rf lib-m1 lib-host obj-m1 obj-host

.PHONY: demo demo-solder sim scl lcd border palette clutcolor clutclock graphics mui stars solder async bit load term clean distclean
