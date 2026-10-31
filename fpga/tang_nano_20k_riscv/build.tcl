# build the bitstream with Gowin EDA, from this directory:
#   gw_sh build.tcl
# output: impl/pnr/clut_rv.fs

set_device -name GW2AR-18C GW2AR-LV18QN88C8/I7

add_file top.v
add_file src/gowin_gorv32/gowin_gorv32.v
add_file rtl/clut_fabric_ahb.v
add_file rtl/ahb_reg_slave.v
add_file rtl/ahb_rd_master.v
add_file ../rtl/spi_lcd.v
add_file ../../rtl/clut_dma.v
add_file tang_nano_20k_riscv.cst
add_file tang_nano_20k_riscv.sdc

set_option -top_module top
set_option -output_base_name clut_rv
set_option -use_sspi_as_gpio 1
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio 1

run all
