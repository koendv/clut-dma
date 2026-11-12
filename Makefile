# clut_dma mock-up: compare the RTL with u8x8_clut_stream_rgb565/444() of u8g2.
# U8G2: a u8g2 tree with the clut branch, https://github.com/koendv/u8g2

U8G2 ?= ../u8g2
CFLAGS = -O2 -Wall -Wextra -DU8X8_WITH_CLUT -I$(U8G2)/csrc
BUILD = build

test: $(BUILD)/ref $(BUILD)/sim
	cd $(BUILD) && ./ref && vvp -n sim | tee sim.log
	grep -q ' 0 errors' $(BUILD)/sim.log
	cmp $(BUILD)/expected.txt $(BUILD)/out.txt
	@echo PASS

# the engine without cache: the same bytes, 3 reads per tile and pixel row
test-nocache: $(BUILD)/ref $(BUILD)/sim_nocache
	mkdir -p $(BUILD)/nocache
	cd $(BUILD)/nocache && ../ref && vvp -n ../sim_nocache | tee sim.log
	grep -q ' 0 errors' $(BUILD)/nocache/sim.log
	cmp $(BUILD)/nocache/expected.txt $(BUILD)/nocache/out.txt
	@echo PASS

# 2 bits per pixel, clut_dma parameter BPP2 = 1, with and without cache: the
# tests of make test, then the 2 bpp tests of ref bpp2
test-bpp2: $(BUILD)/ref $(BUILD)/sim_bpp2 $(BUILD)/sim_bpp2_nocache
	mkdir -p $(BUILD)/bpp2
	cd $(BUILD)/bpp2 && ../ref bpp2
	cd $(BUILD)/bpp2 && vvp -n ../sim_bpp2 | tee sim.log
	grep -q ' 0 errors' $(BUILD)/bpp2/sim.log
	cmp $(BUILD)/bpp2/expected.txt $(BUILD)/bpp2/out.txt
	cd $(BUILD)/bpp2 && vvp -n ../sim_bpp2_nocache | tee sim0.log
	grep -q ' 0 errors' $(BUILD)/bpp2/sim0.log
	cmp $(BUILD)/bpp2/expected.txt $(BUILD)/bpp2/out.txt
	@echo PASS

# without the vertical layout, 2 bits per pixel (VERT = 0, BPP2 = 1): the
# engine for libraries that draw in the horizontal layout, such as LVGL.
# the horizontal tests of make test and the 2 bpp tests
test-lvgl: $(BUILD)/ref $(BUILD)/sim_lvgl
	mkdir -p $(BUILD)/lvgl
	cd $(BUILD)/lvgl && ../ref lvgl
	cd $(BUILD)/lvgl && vvp -n ../sim_lvgl | tee sim.log
	grep -q ' 0 errors' $(BUILD)/lvgl/sim.log
	cmp $(BUILD)/lvgl/expected.txt $(BUILD)/lvgl/out.txt
	@echo PASS

# spi underrun: whole screens, spi P cycles per byte, W memory wait states
SPI_CYCLES = 8 16 32
# external memory, e.g. PSRAM over QSPI: make measure WAIT_STATES="16 24 32"
WAIT_STATES = 0 1 2 4 8 16

# whole screens, a rectangle 1 tile wide on a 320x240 display, whole screens
# in vertical layout, and whole screens in 2 bpp (sim_bpp2)
measure: $(BUILD)/ref $(BUILD)/sim $(BUILD)/sim_bpp2
	for m in measure narrow vmeasure bmeasure; do \
	  s=sim; [ $$m = bmeasure ] && s=sim_bpp2; \
	  mkdir -p $(BUILD)/$$m && (cd $(BUILD)/$$m && ../ref $$m) && \
	  for p in $(SPI_CYCLES); do for w in $(WAIT_STATES); do \
	    (cd $(BUILD)/$$m && echo "$$m: `vvp -n ../$$s +spi=$$p +ws=$$w | grep -E 'measure|errors' | tr '\n' ' '`" && \
	     cmp -s expected.txt out.txt || echo "output differs: $$m spi $$p ws $$w"); \
	  done; done; \
	done

# verilator lint of all rtl
lint:
	verilator --lint-only -Wall rtl/clut_dma.v
	verilator --lint-only -Wall -GCACHE=0 rtl/clut_dma.v
	verilator --lint-only -Wall -GBPP2=1 rtl/clut_dma.v
	verilator --lint-only -Wall -GBPP2=1 -GCACHE=0 rtl/clut_dma.v
	verilator --lint-only -Wall -GBPP2=1 -GVERT=0 rtl/clut_dma.v
	verilator --lint-only -Wall --top-module clut_fabric $(FABRIC_RTL)
	verilator --lint-only -Wall fpga/tang_nano_20k_arm/rtl/apb_reg_slave.v
	verilator --lint-only -Wall fpga/tang_nano_20k_riscv/rtl/ahb_reg_slave.v
	verilator --lint-only -Wall fpga/tang_nano_20k_riscv/rtl/ahb_rd_master.v

# GoRV32 build: clut_dma behind its AHB-Lite adapters; the tests of make
# test, and a bus error
AHB_RTL = rtl/clut_dma.v fpga/tang_nano_20k_riscv/rtl/ahb_reg_slave.v fpga/tang_nano_20k_riscv/rtl/ahb_rd_master.v

ahb: $(BUILD)/ref $(BUILD)/ahb_sim
	mkdir -p $(BUILD)/ahb
	cd $(BUILD)/ahb && ../ref && vvp -n ../ahb_sim | tee sim.log
	grep -q ' 0 errors' $(BUILD)/ahb/sim.log
	cmp $(BUILD)/ahb/expected.txt $(BUILD)/ahb/out.txt
	@echo PASS

$(BUILD)/ahb_sim: $(AHB_RTL) tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -DAHB -o $@ $^

# fpga fabric: clut_dma, fbmem and spi_lcd, as the cpu uses them
FABRIC_RTL = fpga/tang_nano_20k_arm/rtl/clut_fabric.v fpga/tang_nano_20k_arm/rtl/apb_reg_slave.v \
	fpga/rtl/spi_lcd.v fpga/rtl/fbmem.v rtl/clut_dma.v

fabric: $(BUILD)/ref $(BUILD)/fabric_sim
	mkdir -p $(BUILD)/fabric
	cd $(BUILD)/fabric && ../ref fabric && vvp -n ../fabric_sim | tee sim.log
	grep -q ' 0 errors' $(BUILD)/fabric/sim.log
	cmp $(BUILD)/fabric/expected.txt $(BUILD)/fabric/out.txt
	@echo PASS

$(BUILD)/fabric_sim: $(FABRIC_RTL) fpga/tb/tb_fabric.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -o $@ $^

# size of clut_dma in gate equivalents, CACHE = 1 and 0, then with 2 bpp
# (BPP2 = 1), and with 2 bpp without the vertical layout (VERT = 0): yosys to
# generic gates, weights in tb/ge.awk (clut-dma.md section 4.4)
SIZE_CONFIGS = CACHE=1 CACHE=0 CACHE=1,BPP2=1 CACHE=1,BPP2=1,VERT=0

size:
	mkdir -p $(BUILD)
	for c in $(SIZE_CONFIGS); do \
	  yosys -q -p "read_verilog rtl/clut_dma.v; chparam `echo $$c | sed 's/\([A-Z0-9]*\)=\([0-9]\)/-set \1 \2/g; s/,/ /g'` clut_dma; synth -top clut_dma; tee -o $(BUILD)/size-$$c.txt stat" > /dev/null && \
	  echo "$$c: `awk -f tb/ge.awk $(BUILD)/size-$$c.txt`"; \
	done

# formal proofs and covers with SymbiYosys, the short tasks (minutes).
# nightly.sh runs the long ones.
formal:
	cd formal && sby -f clut_dma.sby && sby -f fbmem.sby && sby -f ahb.sby && sby -f apb.sby

pdf: clut-dma.pdf README.pdf

clut-dma.pdf: clut-dma.md fig/data-model.svg fig/engine.svg
	pandoc --from markdown+east_asian_line_breaks $< -o $@ --pdf-engine=xelatex

README.pdf: README.md
	pandoc $< -o $@ --pdf-engine=xelatex -V geometry:margin=2.2cm -V fontsize=10pt -V colorlinks=true

# the files accompanying the publication: the last commit, without uncommitted changes
zip:
	mkdir -p $(BUILD)
	git archive --prefix=clut-dma/ -o $(BUILD)/clut-dma.zip HEAD
	@echo "$(BUILD)/clut-dma.zip: commit `git rev-parse HEAD`"
	@git diff --quiet HEAD || echo "warning: uncommitted changes, not in the zip"

# the binaries of the last commit, in their directories: the bitstreams and
# the firmware of both fpga builds. the bitstreams must be up to date (Gowin
# EDA, make in fpga/tang_nano_20k_*); the firmware is built anew, solder.elf
# in color as make demo-solder runs it
BIN_FW = fw scl lcd border palette clutcolor clutclock graphics mui stars

binzip:
	@git diff --quiet HEAD || { echo "binzip: uncommitted changes"; exit 1; }
	for b in arm riscv; do \
	  $(MAKE) -s -q -C fpga/tang_nano_20k_$$b || { echo "binzip: bitstream of $$b not up to date"; exit 1; }; \
	  cd fpga/tang_nano_20k_$$b/fw && rm -f *.elf && \
	  $(MAKE) -s U8G2=$(abspath $(U8G2)) $(addsuffix .elf,$(BIN_FW)) > /dev/null && \
	  $(MAKE) -s U8G2=$(abspath $(U8G2)) solder.elf COLOR=1 > /dev/null && cd ../../.. || exit 1; \
	done
	mkdir -p $(BUILD)
	rm -f $(BUILD)/clut-dma-bin-`git rev-parse --short HEAD`.zip
	zip -q -X $(BUILD)/clut-dma-bin-`git rev-parse --short HEAD`.zip \
	  fpga/tang_nano_20k_arm/impl/pnr/clut_m1.fs fpga/tang_nano_20k_riscv/impl/pnr/clut_rv.fs \
	  fpga/tang_nano_20k_arm/fw/*.elf fpga/tang_nano_20k_riscv/fw/*.elf
	@ls -l $(BUILD)/clut-dma-bin-`git rev-parse --short HEAD`.zip

$(BUILD)/ref: tb/ref.c $(U8G2)/csrc/u8x8_clut.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/sim: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -o $@ $^

$(BUILD)/sim_nocache: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -P tb_clut_dma.CACHE=0 -o $@ $^

$(BUILD)/sim_bpp2: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -P tb_clut_dma.BPP2=1 -o $@ $^

$(BUILD)/sim_bpp2_nocache: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -P tb_clut_dma.BPP2=1 -P tb_clut_dma.CACHE=0 -o $@ $^

$(BUILD)/sim_lvgl: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -P tb_clut_dma.BPP2=1 -P tb_clut_dma.VERT=0 -o $@ $^

clean:
	rm -rf $(BUILD)

.PHONY: test test-nocache test-bpp2 test-lvgl ahb measure size formal fabric lint pdf zip binzip clean
