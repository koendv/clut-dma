create_clock -name clk27 -period 37.037 [get_ports {clk27}]
create_generated_clock -name hclk -source [get_ports {clk27}] -multiply_by 2 [get_nets {hclk}]
// serial wire debug clock, up to 10 MHz. the M1 debug port synchronizes to hclk
create_clock -name swclk -period 100 [get_ports {swclk}]
set_clock_groups -asynchronous -group [get_clocks {swclk}] -group [get_clocks {hclk}]
