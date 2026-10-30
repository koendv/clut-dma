# Tang Nano 20K bitstream: Gowin Cortex-M1 with clut_fabric
#   make        build impl/pnr/clut_m1.fs with Gowin EDA
#   make load   load the bitstream into the fpga (sram, lost at power off)
#   make flash  write the bitstream to the flash of the board

GWPATH ?= /opt/Gowin_V1.9.12.02_SP2
BITSTREAM = impl/pnr/clut_m1.fs

# gw_sh needs the system freetype, as in gw_ide.sh of Gowin
GW_SH = env LD_LIBRARY_PATH=$(GWPATH)/IDE/lib \
	LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libfreetype.so.6 \
	$(GWPATH)/IDE/bin/gw_sh

SRC = build.tcl top.v tang_nano_20k.cst tang_nano_20k.sdc \
	src/gowin_empu_m1/gowin_empu_m1.v \
	../rtl/clut_fabric.v ../rtl/spi_lcd.v ../rtl/fbmem.v ../../rtl/clut_dma.v

$(BITSTREAM): $(SRC)
	$(GW_SH) build.tcl > build.log 2>&1 || (tail -20 build.log; false)
	@grep -h 'WARN\|ERROR' build.log | sort -u || true

load: $(BITSTREAM)
	openFPGALoader -b tangnano20k $(BITSTREAM)

flash: $(BITSTREAM)
	openFPGALoader -b tangnano20k -f $(BITSTREAM)

clean:
	rm -rf impl build.log

.PHONY: load flash clean
