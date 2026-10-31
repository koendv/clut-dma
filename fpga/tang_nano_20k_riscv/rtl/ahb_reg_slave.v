// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// ahb_reg_slave: AHB-Lite slave for the register interface of clut_dma.
//   writes: no wait states; reg_wr in the data phase, with HWDATA.
//   reads:  one wait state; reg_rd in the first data phase cycle, reg_rdata
//           is HRDATA in the second. So a read right after a write sees the
//           written value.
//   word transfers only: a byte or halfword transfer gets an ERROR response
//   and does not reach the registers.
// The registers are at bits 4:2 of HADDR: they repeat every 32 bytes in the
// address range of HSEL. HBURST, HPROT and HMASTLOCK are not used; bursts
// are transfers one by one.

module ahb_reg_slave (
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
  output reg        hresp,

  // register interface
  output            reg_wr,
  output            reg_rd,
  output     [4:2]  reg_addr,
  output     [31:0] reg_wdata,
  input      [31:0] reg_rdata
);

  wire unused = &{ 1'b0, haddr[31:5], haddr[1:0], htrans[0] };

  wire a_start = hsel && htrans[1] && hready;   // address phase of a transfer
  wire a_bad   = hsize != 3'd2;                  // not a word

  // data phase. d_wait: the first cycle of a read or of ERROR
  reg       d_valid, d_write, d_bad, d_wait;
  reg [4:2] d_addr;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      d_valid <= 0; d_write <= 0; d_bad <= 0; d_wait <= 0; d_addr <= 0;
      hreadyout <= 1; hresp <= 0;
    end else if (hready) begin
      d_valid   <= a_start;
      d_write   <= hwrite;
      d_bad     <= a_bad;
      d_addr    <= haddr[4:2];
      d_wait    <= a_start && (a_bad || !hwrite);
      hreadyout <= !(a_start && (a_bad || !hwrite));
      hresp     <= a_start && a_bad;
    end else if (d_wait) begin
      // second cycle of a read or of ERROR
      d_wait    <= 0;
      hreadyout <= 1;
    end
  end

  assign reg_wr    = d_valid && d_write && !d_bad && hready;
  assign reg_rd    = d_valid && !d_write && !d_bad && d_wait;
  assign reg_addr  = d_addr;
  assign reg_wdata = hwdata;
  assign hrdata    = reg_rdata;

endmodule
