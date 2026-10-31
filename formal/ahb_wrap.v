// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// The AHB-Lite adapters of the GoRV32 build, each on its own, all other
// inputs free:
//   ahb_reg_slave against fahbl_slave, and its register interface: a reg_wr
//     for each word write, a reg_rd for each word read, at the address of
//     the address phase, ERROR for other sizes.
//   ahb_rd_master against fahbl_master, and its clut_dma read port. The read
//     port assumptions here are what clut_dma.sby proves of clut_dma; the
//     read port assertions here include what clut_dma.sby assumes.

module ahb_reg_slave_wrap (
  input         clk,
  input         rst_n,
  input         hsel,
  input  [31:0] haddr,
  input  [1:0]  htrans,
  input         hwrite,
  input  [2:0]  hsize,
  input  [31:0] hwdata,
  input         hready,
  input  [31:0] reg_rdata
);

  wire        hreadyout, hresp, reg_wr, reg_rd;
  wire [31:0] hrdata, reg_wdata;
  wire [4:2]  reg_addr;

  ahb_reg_slave dut (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata)
  );

  // reads and ERROR have one wait state, writes none
  fahbl_slave #(.AW(32), .MAXWAIT(1), .ERROR(1)) ahb (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp)
  );

  // data phase of a transfer to this slave, from the address phase
  reg       f_dx = 0, f_dw = 0, f_dword = 0;
  reg [4:2] f_daddr = 0;
  always @(posedge clk)
    if (!rst_n)
      f_dx <= 0;
    else if (hready) begin
      f_dx    <= hsel && htrans[1];
      f_dw    <= hwrite;
      f_dword <= hsize == 3'd2;
      f_daddr <= haddr[4:2];
    end

  always @(*)
    if (rst_n) begin
      // a word write: reg_wr in its data phase, which has no wait state
      assert(reg_wr == (f_dx && f_dw && f_dword && hreadyout));
      // a word read: reg_rd in the first cycle of its data phase, a wait
      // state; HRDATA is reg_rdata in the second
      assert(reg_rd == (f_dx && !f_dw && f_dword && !hreadyout));
      if (reg_wr || reg_rd)
        assert(reg_addr == f_daddr);
      if (reg_wr)
        assert(reg_wdata == hwdata);
      assert(hrdata == reg_rdata);
      // other sizes: ERROR
      if (f_dx && hresp)
        assert(!f_dword);
      if (f_dx && hreadyout && !f_dword)
        assert(hresp);
    end

  always @(posedge clk)
    if (rst_n) begin
      cover(reg_rd);
      cover(f_dx && hreadyout && hresp);
    end

endmodule

module ahb_rd_master_wrap (
  input         clk,
  input         rst_n,
  input         rd_req,
  input  [31:0] rd_addr,
  input         hready,
  input  [31:0] hrdata,
  input         hresp
);

  wire        rd_ack, rd_err, hwrite;
  wire [31:0] rd_data, haddr, hwdata;
  wire [1:0]  htrans;
  wire [2:0]  hsize, hburst;
  wire [3:0]  hprot;

  ahb_rd_master dut (
    .clk(clk), .rst_n(rst_n),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack),
    .rd_data(rd_data), .rd_err(rd_err),
    .haddr(haddr), .htrans(htrans), .hwrite(hwrite), .hsize(hsize),
    .hburst(hburst), .hprot(hprot), .hwdata(hwdata),
    .hready(hready), .hrdata(hrdata), .hresp(hresp)
  );

  fahbl_master #(.AW(32), .SINGLE(1)) ahb (
    .clk(clk), .rst_n(rst_n),
    .haddr(haddr), .htrans(htrans), .hwrite(hwrite), .hsize(hsize),
    .hburst(hburst), .hprot(hprot), .hwdata(hwdata),
    .hready(hready), .hrdata(hrdata), .hresp(hresp)
  );

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // clut_dma, as proven in clut_dma.sby: no request in reset, rd_addr word
  // aligned, rd_req and rd_addr held until rd_ack
  always @(*) begin
    if (!rst_n)
      assume(!rd_req);
    if (rd_req)
      assume(rd_addr[1:0] == 2'b00);
  end
  always @(posedge clk)
    if (f_past_valid && rst_n && $past(rst_n) && $past(rd_req && !rd_ack))
      assume(rd_req && $stable(rd_addr));

  // data phase
  reg f_dx = 0;
  always @(posedge clk)
    if (!rst_n)
      f_dx <= 0;
    else if (hready)
      f_dx <= htrans[1];

  always @(*)
    if (rst_n) begin
      // as clut_dma.sby assumes: rd_ack only during a request
      if (rd_ack)
        assert(rd_req);
      // reads only: a transfer only for a request, at its address, not in
      // the cycle of rd_ack
      assert(!hwrite);
      if (htrans[1])
        assert(rd_req && !rd_ack && haddr == rd_addr && hsize == 3'd2);
      // one transfer at a time
      assert(!(htrans[1] && f_dx));
    end

  always @(posedge clk)
    if (f_past_valid && rst_n && $past(rst_n)) begin
      // as clut_dma.sby assumes: rd_ack not in two cycles in a row
      if ($past(rd_ack))
        assert(!rd_ack);
      // rd_ack in the cycle after the data phase ended, with its data and
      // response
      assert(rd_ack == $past(f_dx && hready));
      if (rd_ack)
        assert(rd_data == $past(hrdata) && rd_err == $past(hresp));
    end

  always @(posedge clk)
    if (rst_n) begin
      cover(rd_ack && rd_err);
      cover(rd_ack && $past(rd_ack, 4));
    end

endmodule
