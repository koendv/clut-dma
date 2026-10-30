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
#   make sim     the image the display ought to show, from a simulation of the
#                RTL: sim/frame_NN.png per request, sim/final.png.
#                make sim TEST=border, palette, clutcolor, clutclock, graphics, mui
#
# U8G2: a u8g2 tree with the clut branch, https://github.com/koendv/u8g2

CROSS   ?= /opt/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin/arm-none-eabi-
OPENOCD ?= /opt/xpack-openocd-0.12.0-4/bin/openocd
TTY     ?= /dev/ttyUSB1
U8G2    ?= ../../../../u8g2

CC      = $(CROSS)gcc
AR      = $(CROSS)ar
CFLAGS  = -mcpu=cortex-m1 -mthumb -Os -g -Wall -Wextra -ffreestanding \
	-ffunction-sections -fdata-sections
# spi clock: make -B LCD_DIV=64 load
ifdef LCD_DIV
CFLAGS += -DLCD_DIV=$(LCD_DIV)u
endif
# backlight: make -B LCD_BRIGHT=128 load, BRIGHT/256 of the time on (0: always)
ifdef LCD_BRIGHT
CFLAGS += -DLCD_BRIGHT=$(LCD_BRIGHT)u
endif
# u8g2 page buffer of ClutClock and demo_graphics: make PAGE_ROWS=1 clutclock,
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

FW_SRC = start.c main.c testimg.c
FW_DEP = $(FW_SRC) testimg.h hw.h m1.ld lib-m1/libu8g2.a

fw.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -o $@ $(FW_SRC) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# test variants: make scl, make lcd, make border, make palette
scl.elf lcd.elf border.elf palette.elf clutcolor.elf clutclock.elf graphics.elf mui.elf: %.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -D$(shell echo $* | tr a-z A-Z)_TEST -o $@ $(FW_SRC) $(LDFLAGS) lib-m1/libu8g2.a -lc -lgcc

scl lcd border palette clutcolor clutclock graphics mui: %: %.elf
	$(OPENOCD) -f ../m1.cfg -c "init; m1_run $<; shutdown"

# simulation: host.c writes fbmem and the steps of the test, tb_frame.v runs
# them through clut_fabric, st7789.py draws the spi bytes
HOSTCC   ?= cc
IVERILOG ?= iverilog
VVP      ?= vvp
SIMRTL    = ../../../../rtl/clut_dma.v ../../../rtl/clut_fabric.v \
	../../../rtl/spi_lcd.v ../../../rtl/fbmem.v

sim: host.c testimg.c testimg.h ../../tb/tb_frame.v ../../tb/st7789.py lib-host/libu8g2.a
	mkdir -p sim
	$(HOSTCC) -O2 -Wall -Wextra $(SIMDEFS) $(U8G2_DEFS) -o sim/host host.c testimg.c lib-host/libu8g2.a
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

term:
	minicom -D $(TTY) -b 115200

clean:
	rm -f *.elf *.map
	rm -rf sim

distclean: clean
	rm -rf lib-m1 lib-host

.PHONY: demo sim scl lcd border palette clutcolor clutclock graphics mui bit load term clean distclean
