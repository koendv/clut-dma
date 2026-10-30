// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// APB3 port of clut_dma and spi_lcd against fapb3_slave. All other inputs
// are free.

module apb_clut_dma #(
  parameter CACHE = 1
) (
  input         clk,
  input         rst_n,
  input         psel,
  input         penable,
  input         pwrite,
  input  [4:0]  paddr,
  input  [31:0] pwdata,
  input         rd_ack,
  input  [31:0] rd_data,
  input         byte_ready
);

  wire [31:0] prdata;
  wire        pready, pslverr;

  clut_dma #(.CACHE(CACHE)) dut (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .irq(),
    .rd_req(), .rd_addr(), .rd_ack(rd_ack), .rd_data(rd_data),
    .byte_valid(), .byte_data(), .byte_ready(byte_ready)
  );

  fapb3_slave #(.AW(5), .MAXWAIT(0)) apb (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .pready(pready), .prdata(prdata), .pslverr(pslverr)
  );

endmodule

// a write to DATA waits while the data register is full: at most one byte,
// 16 * DIV cycles. in stream mode it is an error. The invariants in the
// FORMAL block of spi_lcd bound the wait with the wait counter of the
// checker, f_wait. DIVMAX limits the DIV values the cpu writes, to keep the
// cover traces short; the reset value is 4
module apb_spi_lcd #(
  parameter DIVMAX = 255
) (
  input         clk,
  input         rst_n,
  input         psel,
  input         penable,
  input         pwrite,
  input  [3:0]  paddr,
  input  [31:0] pwdata,
  input         byte_valid,
  input  [7:0]  byte_data
);

  wire [31:0] prdata;
  wire        pready, pslverr;
  wire [31:0] f_wait;

  spi_lcd dut (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(),
    .sck(), .mosi(), .dc(), .cs_n(), .lcd_rst_n(), .bl(),
    .f_wait(f_wait)
  );

  localparam DMAX    = DIVMAX > 4 ? DIVMAX : 4;
  localparam MAXWAIT = 16 * DMAX + 4;

  always @(*)
    if (psel && pwrite && paddr[3:2] == 2'd3)
      assume(pwdata[7:0] <= DIVMAX);

  fapb3_slave #(.AW(4), .MAXWAIT(MAXWAIT), .SLVERR(1)) apb (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .pready(pready), .prdata(prdata), .pslverr(pslverr),
    .f_wait(f_wait)
  );

endmodule
