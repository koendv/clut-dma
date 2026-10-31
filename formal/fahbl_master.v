// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// fahbl_master: formal properties of an AHB-Lite master port. Instantiate
// next to the master, with all port signals as inputs. HREADY is the bus
// signal.
//
// After the AMBA 3 AHB-Lite Protocol Specification v1.0, ARM IHI 0033A;
// "par." refers to its paragraphs. Properties marked "not in the spec" are
// choices of this file.
//
// Assumptions, about the slaves and the interconnect:
//   - HREADY high during reset (par. 7.1.2).
//   - The data phase of an IDLE or BUSY transfer: zero wait states, OKAY
//     (par. 3.2).
//   - ERROR is a two-cycle response: HRESP high with HREADY low, then HRESP
//     high with HREADY high; OKAY during other wait states (par. 5.1.2,
//     5.1.3).
//   Not assumed: a bound on the wait states (par. 5.1.2 only recommends 16).
// Assertions, about the master:
//   - HTRANS IDLE during reset (par. 7.1.2).
//   - A NONSEQ or SEQ transfer is at most as wide as the data bus (par. 3.4)
//     and aligned to its size (par. 3.5; a single transfer is a burst of one,
//     par. 3.2).
//   - In a waited transfer, once HTRANS is NONSEQ or SEQ, the master holds
//     HTRANS, HADDR and the control signals until HREADY is high (par. 3.6,
//     3.6.1, 3.6.2).
//   - Write data stable during the wait states of a write (par. 3.6, 6.1.1).
//   - SINGLE = 1: only IDLE and NONSEQ, and HBURST SINGLE (par. 3.5). Not in
//     the spec, which allows bursts: a restriction for a master that has
//     none, to keep the checks short.
// Not checked: read data, which needs the slave's contents.
// Covers: a read with a wait state, an ERROR response, a second transfer.
//
// For SymbiYosys with an asynchronous active-low reset (async2sync, the
// default without multiclock).

module fahbl_master #(
  parameter AW     = 32,
  parameter DW     = 32,
  parameter SINGLE = 1            // no bursts: IDLE and NONSEQ only
) (
  input           clk,
  input           rst_n,
  input  [AW-1:0] haddr,
  input  [1:0]    htrans,
  input           hwrite,
  input  [2:0]    hsize,
  input  [2:0]    hburst,
  input  [3:0]    hprot,
  input  [DW-1:0] hwdata,
  input           hready,
  input  [DW-1:0] hrdata,
  input           hresp
);

  // read data is only of interest to the master's own properties
  wire unused = &{ 1'b0, hrdata };

  localparam [1:0] IDLE = 2'b00, NONSEQ = 2'b10;

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // start in reset
  always @(*)
    if (!f_past_valid)
      assume(!rst_n);

  // out of reset now and in the previous cycle
  reg  f_rst_n_d = 0;
  always @(posedge clk)
    f_rst_n_d <= rst_n;
  wire f_run = f_past_valid && rst_n && f_rst_n_d;

  // data phase: the address phase before it, sampled with HREADY high (par. 3.1)
  reg f_dxfer = 0;                  // a NONSEQ or SEQ data phase
  reg f_dwrite = 0;
  always @(posedge clk)
    if (!rst_n)
      f_dxfer <= 0;
    else if (hready) begin
      f_dxfer  <= htrans[1];
      f_dwrite <= hwrite;
    end

  // slaves: HREADY high during reset (par. 7.1.2)
  always @(*)
    if (!rst_n)
      assume(hready);

  // slaves: IDLE and BUSY get a zero wait state OKAY (par. 3.2)
  always @(*)
    if (rst_n && !f_dxfer)
      assume(hready && !hresp);

  // slaves: response (par. 5.1.2, 5.1.3)
  always @(posedge clk)
    if (f_run) begin
      // first cycle of ERROR, then the second
      if ($past(f_dxfer && hresp && !hready))
        assume(hresp && hready);
      // the second cycle of ERROR follows the first; OKAY during other waits
      if (hresp && hready)
        assume($past(f_dxfer && hresp && !hready));
    end

  // master: IDLE during reset (par. 7.1.2)
  always @(*)
    if (!rst_n)
      assert(htrans == IDLE);

  // master: size and alignment of a transfer (par. 3.2, 3.4, 3.5)
  always @(*)
    if (rst_n && htrans[1]) begin
      assert(8 << hsize <= DW);
      if (hsize == 3'd1)
        assert(haddr[0] == 1'b0);
      if (hsize == 3'd2)
        assert(haddr[1:0] == 2'b00);
    end

  // master: a waited transfer holds address and control (par. 3.6, 3.6.1, 3.6.2)
  always @(posedge clk)
    if (f_run && $past(htrans[1] && !hready)) begin
      assert($stable(htrans));
      assert($stable(haddr));
      assert($stable(hwrite));
      assert($stable(hsize));
      assert($stable(hburst));
      assert($stable(hprot));
    end

  // master: write data stable during the wait states of a write (par. 3.6, 6.1.1)
  always @(posedge clk)
    if (f_run && $past(f_dxfer && f_dwrite && !hready))
      assert($stable(hwdata));

  // master: no bursts. not in the spec, a restriction of this file (par. 3.5)
  generate if (SINGLE) begin : single
    always @(*)
      if (rst_n) begin
        assert(htrans == IDLE || htrans == NONSEQ);
        if (htrans[1])
          assert(hburst == 3'd0);
      end
  end endgenerate

  // the assumptions leave room for transfers
  reg [1:0] f_ndone = 0;            // transfers ended, up to 2
  always @(posedge clk)
    if (!rst_n)
      f_ndone <= 0;
    else if (f_dxfer && hready && f_ndone != 2'd2)
      f_ndone <= f_ndone + 2'd1;

  always @(posedge clk)
    if (f_run) begin
      cover(f_dxfer && hready && !f_dwrite && $past(f_dxfer && !hready));
      cover(f_dxfer && hready && hresp);
      cover(f_dxfer && hready && f_ndone == 2'd1);
    end

endmodule
