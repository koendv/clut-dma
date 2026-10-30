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

# spi underrun: whole screens, spi P cycles per byte, W memory wait states
SPI_CYCLES = 8 16 32
# external memory, e.g. PSRAM over QSPI: make measure WAIT_STATES="16 24 32"
WAIT_STATES = 0 1 2 4 8 16

# whole screens, and a rectangle 1 tile wide on a 320x240 display
measure: $(BUILD)/ref $(BUILD)/sim
	for m in measure narrow; do \
	  mkdir -p $(BUILD)/$$m && (cd $(BUILD)/$$m && ../ref $$m) && \
	  for p in $(SPI_CYCLES); do for w in $(WAIT_STATES); do \
	    (cd $(BUILD)/$$m && echo "$$m: `vvp -n ../sim +spi=$$p +ws=$$w | grep -E 'measure|errors' | tr '\n' ' '`" && \
	     cmp -s expected.txt out.txt || echo "output differs: $$m spi $$p ws $$w"); \
	  done; done; \
	done

# verilator lint of all rtl
lint:
	verilator --lint-only -Wall rtl/clut_dma.v
	verilator --lint-only -Wall -GCACHE=0 rtl/clut_dma.v
	verilator --lint-only -Wall --top-module clut_fabric $(FABRIC_RTL)

# fpga fabric: clut_dma, fbmem and spi_lcd, as the cpu uses them
FABRIC_RTL = fpga/rtl/clut_fabric.v fpga/rtl/spi_lcd.v fpga/rtl/fbmem.v rtl/clut_dma.v

fabric: $(BUILD)/ref $(BUILD)/fabric_sim
	mkdir -p $(BUILD)/fabric
	cd $(BUILD)/fabric && ../ref fabric && vvp -n ../fabric_sim | tee sim.log
	grep -q ' 0 errors' $(BUILD)/fabric/sim.log
	cmp $(BUILD)/fabric/expected.txt $(BUILD)/fabric/out.txt
	@echo PASS

$(BUILD)/fabric_sim: $(FABRIC_RTL) fpga/tb/tb_fabric.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -o $@ $^

# size of clut_dma in gate equivalents, CACHE = 1 and 0: yosys to generic
# gates, weights in tb/ge.awk (clut-dma.md section 4.4)
size:
	mkdir -p $(BUILD)
	for c in 1 0; do \
	  yosys -q -p "read_verilog rtl/clut_dma.v; chparam -set CACHE $$c clut_dma; synth -top clut_dma; tee -o $(BUILD)/size$$c.txt stat" > /dev/null && \
	  echo "CACHE=$$c: `awk -f tb/ge.awk $(BUILD)/size$$c.txt`"; \
	done

# formal proofs and covers with SymbiYosys, the short tasks (minutes).
# nightly.sh runs the long ones.
formal:
	cd formal && sby -f apb.sby && sby -f clut_dma.sby && sby -f fbmem.sby

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

$(BUILD)/ref: tb/ref.c $(U8G2)/csrc/u8x8_clut.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/sim: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -o $@ $^

$(BUILD)/sim_nocache: rtl/clut_dma.v tb/tb_clut_dma.v
	mkdir -p $(BUILD)
	iverilog -g2005 -Wall -P tb_clut_dma.CACHE=0 -o $@ $^

clean:
	rm -rf $(BUILD)

.PHONY: test test-nocache measure size formal fabric lint pdf zip clean
