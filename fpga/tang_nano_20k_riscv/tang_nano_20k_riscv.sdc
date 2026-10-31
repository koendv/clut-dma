create_clock -name clk27 -period 37.037 [get_ports {clk27}]
// 27 MHz * 8 / 9 = 24 MHz
create_generated_clock -name hclk -source [get_ports {clk27}] -multiply_by 8 -divide_by 9 [get_nets {hclk}]
// jtag clock of the GoRV32 debug port, up to 5 MHz, as in the reference designs of Gowin
create_clock -name jtag_tck -period 200 [get_ports {jtag_tck}]
set_clock_groups -asynchronous -group [get_clocks {jtag_tck}] -group [get_clocks {hclk}]
