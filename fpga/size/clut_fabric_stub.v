// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_fabric stub: the ports of fpga/rtl/clut_fabric.v, constant outputs, an
// idle bus slave. For the size of the processor alone
// (fpga/tang_nano_20k/size.sh).

module clut_fabric (
  input             clk,
  input             rst_n,
  input             psel,
  input             penable,
  input             pwrite,
  input      [11:0] paddr,
  input      [31:0] pwdata,
  output     [31:0] prdata,
  output            pready,
  output            pslverr,
  input             hsel,
  input      [31:0] haddr,
  input      [1:0]  htrans,
  input             hwrite,
  input      [2:0]  hsize,
  input      [31:0] hwdata,
  input             hready,
  output            hreadyout,
  output     [31:0] hrdata,
  output            hresp,
  output            irq,
  output     [1:0]  gpio_out,
  input             gpio_in,
  output            lcd_sck,
  output            lcd_mosi,
  output            lcd_dc,
  output            lcd_cs_n,
  output            lcd_rst_n,
  output            lcd_bl
);

  assign prdata = 32'd0;
  assign pready = 1'b1;
  assign pslverr = 1'b0;
  assign hreadyout = 1'b1;
  assign hrdata = 32'd0;
  assign hresp = 1'b0;
  assign irq = 1'b0;
  assign gpio_out = 2'd0;
  assign lcd_sck = 1'b0;
  assign lcd_mosi = 1'b0;
  assign lcd_dc = 1'b0;
  assign lcd_cs_n = 1'b1;
  assign lcd_rst_n = 1'b0;
  assign lcd_bl = 1'b0;

endmodule
