// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_dma stub: the ports of rtl/clut_dma.v, constant outputs. For the size of
// clut_dma in the fpga (fpga/tang_nano_20k_arm/size.sh): the whole design with
// this stub, against the whole design.

module clut_dma #(
  parameter CACHE = 1
) (
  input         clk,
  input         rst_n,
  input         reg_wr,
  input         reg_rd,
  input  [4:2]  reg_addr,
  input  [31:0] reg_wdata,
  output [31:0] reg_rdata,
  output        irq,
  output        rd_req,
  output [31:0] rd_addr,
  input         rd_ack,
  input  [31:0] rd_data,
  input         rd_err,
  output        byte_valid,
  output [7:0]  byte_data,
  input         byte_ready
);

  assign reg_rdata = 32'd0;
  assign irq = 1'b0;
  assign rd_req = 1'b0;
  assign rd_addr = 32'd0;
  assign byte_valid = 1'b0;
  assign byte_data = 8'd0;

endmodule
