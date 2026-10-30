// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// fahbl_slave: formal properties of an AHB-Lite slave port. Instantiate next
// to the slave, with all port signals as inputs. HREADY is the bus signal,
// HREADYOUT the slave's own.
//
// After the AMBA 3 AHB-Lite Protocol Specification v1.0, ARM IHI 0033A;
// "par." refers to its paragraphs. Properties marked "not in the spec" are
// choices of this file.
//
// Assumptions, about the master and the interconnect:
//   - HTRANS is IDLE during reset (par. 7.1.2).
//   - A NONSEQ or SEQ transfer to this slave is at most as wide as the data
//     bus (par. 3.4, 6.2.3) and aligned to its size (par. 3.5; a single
//     transfer is a burst of one, par. 3.2).
//   - In the data phase of this slave, HREADY is its HREADYOUT (par. 4.2).
//     Otherwise HREADY is free: another slave's data phase.
//   Not assumed: address, control and write data stable during wait states
//   (par. 3.6, 6.1.1). The slave must sample them only with HREADY high
//   (par. 3.1), so the proof also holds for a master that changes them.
// Assertions, about the slave:
//   - HREADYOUT high during reset (par. 7.1.2).
//   - The data phase of an IDLE or BUSY transfer to this slave: zero wait
//     states, OKAY (par. 3.2).
//   - At most MAXWAIT wait states in a data phase; MAXWAIT < 0: no bound. Not
//     in the spec, which recommends at most 16 (par. 5.1.2).
//   - ERROR is a two-cycle response: HRESP high with HREADYOUT low, then HRESP
//     high with HREADYOUT high; OKAY during other wait states (par. 5.1.2,
//     5.1.3). Never ERROR if the parameter ERROR is 0.
// Not checked: HBURST, HPROT and HMASTLOCK (the slave has no such inputs);
// read data (par. 6.1.2), which needs the slave's contents.
// Covers: a read with a wait state, a write, and two transfers back to back.
//
// For SymbiYosys with an asynchronous active-low reset (async2sync, the
// default without multiclock).

module fahbl_slave #(
  parameter AW      = 32,
  parameter DW      = 32,
  parameter MAXWAIT = 0,          // wait states; < 0: no bound
  parameter ERROR   = 0           // the slave may give an ERROR response
) (
  input           clk,
  input           rst_n,
  input           hsel,
  input  [AW-1:0] haddr,
  input  [1:0]    htrans,
  input           hwrite,
  input  [2:0]    hsize,
  input  [DW-1:0] hwdata,
  input           hready,
  input           hreadyout,
  input  [DW-1:0] hrdata,
  input           hresp
);

  // data buses are only of interest to the slave's own properties
  wire unused = &{ 1'b0, hwdata, hrdata };

  localparam [1:0] IDLE = 2'b00, BUSY = 2'b01;

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // start in reset
  always @(*)
    if (!f_past_valid)
      assume(!rst_n);

  // master: IDLE during reset (par. 7.1.2)
  always @(*)
    if (!rst_n)
      assume(htrans == IDLE);

  // master: size and alignment of a transfer (par. 3.2, 3.4, 3.5, 6.2.3)
  always @(*)
    if (hsel && htrans[1]) begin
      assume(8 << hsize <= DW);
      if (hsize == 3'd1)
        assume(haddr[0] == 1'b0);
      if (hsize == 3'd2)
        assume(haddr[1:0] == 2'b00);
    end

  // data phase: the address phase before it, sampled with HREADY high (par. 3.1)
  reg       f_dsel = 0;               // data phase of this slave
  reg [1:0] f_dtrans = IDLE;
  reg       f_dwrite = 0;
  always @(posedge clk)
    if (!rst_n) begin
      f_dsel   <= 0;
      f_dtrans <= IDLE;
    end else if (hready) begin
      f_dsel   <= hsel;
      f_dtrans <= htrans;
      f_dwrite <= hwrite;
    end

  wire f_dxfer = f_dsel && f_dtrans[1];   // a NONSEQ or SEQ data phase
  wire f_done  = f_dxfer && hreadyout;     // its last cycle

  // interconnect: HREADY is the HREADYOUT of the slave in its data phase (par. 4.2)
  always @(*)
    if (f_dsel)
      assume(hready == hreadyout);

  // slave: HREADYOUT high during reset (par. 7.1.2)
  always @(*)
    if (!rst_n)
      assert(hreadyout);

  // slave: IDLE and BUSY get a zero wait state OKAY (par. 3.2)
  always @(*)
    if (rst_n && f_dsel && !f_dtrans[1])
      assert(hreadyout && !hresp);

  // slave: wait states. the bound is not in the spec (par. 5.1.2)
  generate if (MAXWAIT >= 0) begin : wait_bound
    integer f_wait = 0;
    always @(posedge clk)
      if (!rst_n || !f_dxfer || hreadyout)
        f_wait <= 0;
      else
        f_wait <= f_wait + 1;

    always @(*)
      if (rst_n && f_dxfer && f_wait >= MAXWAIT)
        assert(hreadyout);
  end endgenerate

  // slave: response (par. 5.1.2, 5.1.3)
  always @(*)
    if (rst_n && f_dxfer && !ERROR)
      assert(!hresp);
  always @(posedge clk)
    if (f_past_valid && rst_n && $past(rst_n) && ERROR) begin
      // first cycle of ERROR, then the second
      if ($past(f_dxfer && hresp && !hreadyout))
        assert(f_dxfer && hresp && hreadyout);
      // the second cycle of ERROR follows the first; OKAY during other waits
      if (f_dxfer && hresp && hreadyout)
        assert($past(f_dxfer && hresp && !hreadyout));
    end

  // the assumptions leave room for transfers
  always @(posedge clk)
    if (f_past_valid && rst_n && $past(rst_n)) begin
      cover(f_done && !f_dwrite && $past(f_dxfer && !hreadyout));
      cover(f_done && f_dwrite);
      cover(f_done && $past(f_done));
    end

endmodule
