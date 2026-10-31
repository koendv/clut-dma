# firmware for the Gowin GoRV32 on the Tang Nano 20K
#   make         build fw.elf: the clut_dma test of the Cortex-M1 build
#   make bit     load the bitstream into the fpga (sram)
#   make load    load fw.elf into On-Chip RAM over JTAG and run it
#   make term    serial terminal on UART1, 115200 baud
#   make scl lcd border palette clutcolor clutclock graphics mui stars
#                the tests and demos of fpga/tang_nano_20k_arm/fw/Makefile.
#   make solder  the soldering station demo, 24x24 Chinese (solder.c), a
#                page buffer of PAGE_ROWS tile rows (default 2), black and
#                white; fbmem only the bytes it uses. COLOR=1,
#                USE_CLUT_DMA=0: as in the Cortex-M1 build. key b: a benchmark
#                With compressed instructions all fit in the 32 KiB On-Chip
#                RAM. The 64 KB configuration of GoRV32 is faulty: a byte
#                store at times lands one word further (found with stars).
#   make async   the auto gauge of the Cortex-M1 build (async.c): the whole
#                screen in one clut_dma request, interrupt driven (PLIC)
#   make demo-solder  solder with keys over the uart, for a video
#                (solder_demo.sh of the Cortex-M1 build, the same keys).
#                OPTS: the build options of make solder, default COLOR=1;
#                OPTS= for black and white
# main.c is a copy of the Cortex-M1 main.c with another platform: UART1,
# mcycle, fbmem in On-Chip RAM. testimg.c and testimg.h are those of the
# Cortex-M1 build.
#
# U8G2: a u8g2 tree with the clut branch, https://github.com/koendv/u8g2
# ADAFRUIT_GFX: Adafruit GFX Library, https://github.com/adafruit/Adafruit-GFX-Library
#   (1.13.0), only for make stars; the Arduino headers it needs are in
#   fpga/tang_nano_20k_arm/fw/arduino/

CROSS   ?= /opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-
OPENOCD ?= /opt/xpack-openocd-0.12.0-4/bin/openocd
TTY     ?= /dev/ttyUSB1
U8G2    ?= ../../../../u8g2
ADAFRUIT_GFX ?= $(HOME)/Arduino/libraries/Adafruit_GFX_Library
M1FW    = ../../tang_nano_20k_arm/fw

CC      = $(CROSS)gcc
CXX     = $(CROSS)g++
AR      = $(CROSS)ar
# rv32imc: the GoRV32 SDK uses rv32im; with c, graphics and mui fit in 32 KiB
CFLAGS  = -march=rv32imc_zicsr -mabi=ilp32 -Os -g -Wall -Wextra -ffreestanding \
	-ffunction-sections -fdata-sections -I. -I$(M1FW)
ifdef LCD_DIV
CFLAGS += -DLCD_DIV=$(LCD_DIV)u
endif
ifdef LCD_BRIGHT
CFLAGS += -DLCD_BRIGHT=$(LCD_BRIGHT)u
endif
ifdef PAGE_ROWS
CFLAGS += -DPAGE_ROWS=$(PAGE_ROWS)
endif
# the demos that send through u8g2 (clutclock graphics mui solder): clut_dma
# sends the pixels, u8x8_clut_dma.c in place of the software of u8g2;
# USE_CLUT_DMA=0: the cpu expands the colors. make solder COLOR=1: a
# tile map and a clut (fpga/tang_nano_20k_arm/fw/Makefile)
USE_CLUT_DMA ?= 1
ifeq ($(USE_CLUT_DMA),1)
CFLAGS += -DUSE_CLUT_DMA
endif
ifdef COLOR
CFLAGS += -DSOLDER_COLOR
endif
ifdef LCD_MADCTL
CFLAGS += -DLCD_MADCTL=$(LCD_MADCTL)u
endif
CXXFLAGS = $(filter-out -ffreestanding,$(CFLAGS)) -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-use-cxa-atexit
GFX_DEFS = -DARDUINO=100 -I$(M1FW)/arduino -I$(ADAFRUIT_GFX)
GFX_HDR  = $(M1FW)/arduino/Arduino.h $(M1FW)/arduino/Print.h $(M1FW)/testimg.h
LDFLAGS = -nostartfiles --specs=nano.specs -T rv.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(@:.elf=.map)

U8G2_DEFS = -DU8X8_WITH_CLUT -DMUI_U8G2_HIRES -I$(U8G2)/csrc
U8G2_SRC  = $(abspath $(filter-out %/u8x8_d_gc9a01.c,$(wildcard $(U8G2)/csrc/*.c)))

lib-rv/libu8g2.a: $(U8G2_SRC)
	mkdir -p lib-rv
	cd lib-rv && for f in $(U8G2_SRC); do $(CC) $(CFLAGS) $(U8G2_DEFS) -w -c $$f || exit 1; done
	$(AR) rcs $@ lib-rv/*.o

# u8x8_clut_dma.c an object on the command line, not in a library: it
# replaces a weak function of libu8g2.a
FW_SRC = start.S main.c $(M1FW)/testimg.c $(M1FW)/u8x8_clut_dma.c
FW_DEP = $(FW_SRC) $(M1FW)/testimg.h hw.h rv.ld lib-rv/libu8g2.a

fw.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -o $@ $(FW_SRC) $(LDFLAGS) lib-rv/libu8g2.a -lc -lgcc
	$(CROSS)size $@

scl.elf lcd.elf border.elf palette.elf clutcolor.elf clutclock.elf graphics.elf mui.elf: %.elf: $(FW_DEP)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -D$(shell echo $* | tr a-z A-Z)_TEST -o $@ $(FW_SRC) $(LDFLAGS) lib-rv/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# the soldering station demo: 24x24 Chinese, a page buffer, black and white
SOLDER_SRC = $(M1FW)/solder.c $(M1FW)/font/u8g2_font_gb24st_t_solder.c

solder.elf: $(FW_DEP) $(SOLDER_SRC)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DSOLDER_TEST -o $@ $(FW_SRC) $(SOLDER_SRC) $(LDFLAGS) lib-rv/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# the auto gauge: one request per screen, interrupt driven
async.elf: $(FW_DEP) $(M1FW)/async.c
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DASYNC_TEST -o $@ $(FW_SRC) $(M1FW)/async.c $(LDFLAGS) lib-rv/libu8g2.a -lc -lgcc
	$(CROSS)size $@

# Adafruit GFX and the demo, c++, not in the u8g2 libraries
obj-rv/Adafruit_GFX.o: $(ADAFRUIT_GFX)/Adafruit_GFX.cpp $(GFX_HDR)
	mkdir -p obj-rv
	$(CXX) $(CXXFLAGS) $(GFX_DEFS) -w -c -o $@ $<

obj-rv/stars.o: $(M1FW)/stars.cpp $(GFX_HDR)
	mkdir -p obj-rv
	$(CXX) $(CXXFLAGS) $(GFX_DEFS) -c -o $@ $<

STARS_OBJ = obj-rv/stars.o obj-rv/Adafruit_GFX.o

stars.elf: $(FW_DEP) $(STARS_OBJ)
	$(CC) $(CFLAGS) $(U8G2_DEFS) -DSTARS_TEST -o $@ $(FW_SRC) $(STARS_OBJ) $(LDFLAGS) lib-rv/libu8g2.a -lc -lgcc
	$(CROSS)size $@

scl lcd border palette clutcolor clutclock graphics mui stars solder async: %: %.elf
	$(OPENOCD) -f ../rv.cfg -c "init; rv_run $<; shutdown"

bit:
	$(MAKE) -C .. load

load: fw.elf
	$(OPENOCD) -f ../rv.cfg -c "init; rv_run fw.elf; shutdown"

# the soldering station with keys over the uart, for a video
OPTS ?= COLOR=1
demo-solder:
	OPTS='$(OPTS)' TTY='$(TTY)' $(M1FW)/solder_demo.sh

term:
	minicom -D $(TTY) -b 115200

clean:
	rm -f *.elf *.map

distclean: clean
	rm -rf lib-rv obj-rv

.PHONY: scl lcd border palette clutcolor clutclock graphics mui stars solder demo-solder bit load term clean distclean
