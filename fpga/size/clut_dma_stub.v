// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_dma stub: the ports of rtl/clut_dma.v, constant outputs. For the size of
// clut_dma in the fpga (fpga/tang_nano_20k/size.sh): the whole design with
// this stub, against the whole design.

module clut_dma #(
  parameter CACHE = 1
) (
  input         clk,
  input         rst_n,
  input         psel,
  input         penable,
  input         pwrite,
  input  [4:0]  paddr,
  input  [31:0] pwdata,
  output [31:0] prdata,
  output        pready,
  output        pslverr,
  output        irq,
  output        rd_req,
  output [31:0] rd_addr,
  input         rd_ack,
  input  [31:0] rd_data,
  output        byte_valid,
  output [7:0]  byte_data,
  input         byte_ready
);

  assign prdata = 32'd0;
  assign pready = 1'b1;
  assign pslverr = 1'b0;
  assign irq = 1'b0;
  assign rd_req = 1'b0;
  assign rd_addr = 32'd0;
  assign byte_valid = 1'b0;
  assign byte_data = 8'd0;

endmodule
