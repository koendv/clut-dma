// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// fbmem: memory for frame buffer, tile map and clut. dual port block ram.
//   port a: AHB-Lite slave, for the cpu. byte, halfword and word access.
//           one address per cycle: writes without wait states,
//           reads with one wait state.
//   port b: read port of clut_dma. rd_ack and rd_data in the cycle after rd_req.
// Addresses wrap at the memory size: only the low address bits are used.

module fbmem #(
  parameter AW = 12               // 2^AW words of 32 bits, 16 kbyte
) (
  input             clk,
  input             rst_n,

  // AHB-Lite slave
  input             hsel,
  input      [31:0] haddr,
  input      [1:0]  htrans,
  input             hwrite,
  input      [2:0]  hsize,
  input      [31:0] hwdata,
  input             hready,
  output reg        hreadyout,
  output     [31:0] hrdata,
  output            hresp,

  // clut_dma read port
  input             rd_req,
  input      [31:0] rd_addr,
  output reg        rd_ack,
  output     [31:0] rd_data
);

  // a byte lane per block ram
  reg [7:0] mem0 [0:(1 << AW) - 1];
  reg [7:0] mem1 [0:(1 << AW) - 1];
  reg [7:0] mem2 [0:(1 << AW) - 1];
  reg [7:0] mem3 [0:(1 << AW) - 1];

  assign hresp = 1'b0;

  // addresses wrap at the memory size; sequential and non-sequential
  // transfers are the same; clut_dma reads words
  wire unused = &{ 1'b0, haddr[31:AW+2], htrans[0], rd_addr[31:AW+2], rd_addr[1:0] };

  // byte enables of the transfer
  function [3:0] lanes(input [2:0] size, input [1:0] a);
    case (size)
      3'd0:    lanes = 4'b0001 << a;
      3'd1:    lanes = a[1] ? 4'b1100 : 4'b0011;
      default: lanes = 4'b1111;
    endcase
  endfunction

  wire a_start = hsel && htrans[1] && hready;

  // data phase
  reg          d_valid, d_write;
  reg [AW-1:0] d_word;
  reg [3:0]    d_be;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      d_valid <= 0; d_write <= 0; d_word <= 0; d_be <= 0;
      hreadyout <= 1;
    end else begin
      if (hready) begin
        d_valid <= a_start;
        d_write <= hwrite;
        d_word  <= haddr[AW+1:2];
        d_be    <= lanes(hsize, haddr[1:0]);
      end
      // read: the block ram reads in the first data phase cycle
      hreadyout <= !(a_start && !hwrite);
    end
  end

  wire wr = d_valid && d_write && hready;

  // port a, write-through: the GW2A block ram has no read-before-write
  reg [7:0] qa0, qa1, qa2, qa3;
  always @(posedge clk)
    if (wr && d_be[0]) begin
      mem0[d_word] <= hwdata[7:0];
      qa0 <= hwdata[7:0];
    end else
      qa0 <= mem0[d_word];
  always @(posedge clk)
    if (wr && d_be[1]) begin
      mem1[d_word] <= hwdata[15:8];
      qa1 <= hwdata[15:8];
    end else
      qa1 <= mem1[d_word];
  always @(posedge clk)
    if (wr && d_be[2]) begin
      mem2[d_word] <= hwdata[23:16];
      qa2 <= hwdata[23:16];
    end else
      qa2 <= mem2[d_word];
  always @(posedge clk)
    if (wr && d_be[3]) begin
      mem3[d_word] <= hwdata[31:24];
      qa3 <= hwdata[31:24];
    end else
      qa3 <= mem3[d_word];
  assign hrdata = { qa3, qa2, qa1, qa0 };

  // port b
  wire [AW-1:0] b_addr = rd_addr[AW+1:2];
  reg  [7:0]    qb0, qb1, qb2, qb3;
  always @(posedge clk) begin
    qb0 <= mem0[b_addr];
    qb1 <= mem1[b_addr];
    qb2 <= mem2[b_addr];
    qb3 <= mem3[b_addr];
  end
  assign rd_data = { qb3, qb2, qb1, qb0 };

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      rd_ack <= 0;
    else
      rd_ack <= rd_req && !rd_ack;
  end

endmodule
