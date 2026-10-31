// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// ahb_rd_master: AHB-Lite master for the memory read port of clut_dma.
// One read at a time: a single word read, NONSEQ, then the data phase. No
// bursts, no pipelining: the next address phase starts after rd_ack.
//   rd_req, rd_addr: from clut_dma; rd_addr stable and word aligned while
//                    rd_req is high, until rd_ack.
//   rd_ack:          one cycle, in the cycle after the data phase ended.
//   rd_data, rd_err: with rd_ack: HRDATA, and HRESP (ERROR) of the data phase.
// All outputs come from flip-flops, except HADDR, which is rd_addr.

module ahb_rd_master (
  input             clk,
  input             rst_n,

  // clut_dma read port
  input             rd_req,
  input      [31:0] rd_addr,
  output reg        rd_ack,
  output reg [31:0] rd_data,
  output reg        rd_err,

  // AHB-Lite master
  output     [31:0] haddr,
  output     [1:0]  htrans,
  output            hwrite,
  output     [2:0]  hsize,
  output     [2:0]  hburst,
  output     [3:0]  hprot,
  output     [31:0] hwdata,
  input             hready,
  input      [31:0] hrdata,
  input             hresp
);

  reg a_phase;   // address phase: NONSEQ on the bus
  reg d_phase;   // data phase

  assign haddr  = rd_addr;
  assign htrans = { a_phase, 1'b0 };   // NONSEQ or IDLE
  assign hwrite = 1'b0;
  assign hsize  = 3'd2;                // word
  assign hburst = 3'd0;                // SINGLE
  assign hprot  = 4'b0011;             // data, privileged, not bufferable, not cacheable
  assign hwdata = 32'd0;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      a_phase <= 0; d_phase <= 0;
      rd_ack <= 0; rd_data <= 0; rd_err <= 0;
    end else begin
      rd_ack <= 0;
      // a new read: not while one is on the bus or being acknowledged
      if (rd_req && !a_phase && !d_phase && !rd_ack)
        a_phase <= 1;
      // address phase ends: the data phase follows
      if (a_phase && hready) begin
        a_phase <= 0;
        d_phase <= 1;
      end
      // data phase ends
      if (d_phase && hready) begin
        d_phase <= 0;
        rd_ack  <= 1;
        rd_data <= hrdata;
        rd_err  <= hresp;
      end
    end
  end

endmodule
