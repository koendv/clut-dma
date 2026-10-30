// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// fapb3_slave: formal properties of an APB3 slave port. Instantiate next to
// the slave, with all port signals as inputs.
//
// After the AMBA 3 APB Protocol Specification v1.0, ARM IHI 0024B; "par."
// refers to its paragraphs. Properties marked "not in the spec" are choices
// of this file.
//
// Assumptions, about the master:
//   - PSEL is low during reset. Not in the spec; IDLE is the default state
//     (par. 3.1).
//   - A transfer is a setup phase (PSEL, !PENABLE), then an access phase
//     (PSEL, PENABLE) that lasts until PREADY. Then PENABLE goes low: idle,
//     or the setup phase of the next transfer (par. 2.1, 2.2, 3.1).
//   - PADDR, PWRITE and, in a write, PWDATA do not change from the setup
//     phase to the end of the access phase (par. 2.1.2, 2.2.2, 3.1).
//   PENABLE is free while PSEL is low: PSELx is per slave, PENABLE is shared
//   (par. 4.1), so it may belong to another slave's transfer.
// Assertions, about the slave:
//   - PREADY within MAXWAIT wait cycles of the access phase; MAXWAIT < 0: no
//     bound. Not in the spec, which allows any number (par. 2.2.2).
//   - PSLVERR only in the last cycle of the access phase, and never if
//     SLVERR is 0. The spec only recommends PSLVERR low outside the last
//     cycle (par. 2.3); this checks the recommendation.
// Not checked: read data valid in the last cycle (par. 2.2.1); that needs
// the slave's register contents.
// Covers: a write and a read transfer, and one with wait cycles if allowed.
// Output f_wait: the wait cycles so far in the access phase, for invariants
// of the slave that bound them.
//
// For SymbiYosys with an asynchronous active-low reset (async2sync, the
// default without multiclock).

module fapb3_slave #(
  parameter AW      = 32,
  parameter DW      = 32,
  parameter MAXWAIT = 0,          // wait cycles; < 0: no bound
  parameter SLVERR  = 0           // the slave may assert PSLVERR
) (
  input           clk,
  input           rst_n,
  input           psel,
  input           penable,
  input           pwrite,
  input  [AW-1:0] paddr,
  input  [DW-1:0] pwdata,
  input           pready,
  input  [DW-1:0] prdata,
  input           pslverr,
  output reg [31:0] f_wait
);

  // prdata is only of interest to the slave's own properties
  wire unused = &{ 1'b0, prdata };

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // start in reset
  always @(*)
    if (!f_past_valid)
      assume(!rst_n);

  // phases (par. 3.1)
  wire setup  = psel && !penable;
  wire access = psel && penable;
  wire last   = access && pready;   // last cycle of a transfer

  // master: PSEL low during reset. not in the spec (par. 3.1: IDLE is the
  // default state)
  always @(*)
    if (!rst_n)
      assume(!psel);

  // master: phase order. PSEL is low during reset, so the first transfer
  // after reset starts with setup
  always @(posedge clk)
    if (f_past_valid && rst_n) begin
      if ($past(setup))
        assume(access);                       // setup, then access (par. 3.1)
      else if ($past(access) && !$past(pready))
        assume(access);                       // access until PREADY (par. 2.1.2, 2.2.2)
      else if (psel)
        assume(!penable);                     // a transfer starts with setup (par. 2.1.1, 3.1)
    end

  // master: address, direction and write data hold during a transfer
  // (par. 2.1.2, 2.2.2, 3.1)
  always @(posedge clk)
    if (f_past_valid && rst_n && $past(psel) && !$past(last)) begin
      assume($stable(paddr));
      assume($stable(pwrite));
      if (pwrite)
        assume($stable(pwdata));
    end

  // slave: wait cycles of the access phase. PREADY is free outside the
  // access phase (par. 2.1.2). the bound is not in the spec (par. 2.2.2)
  initial f_wait = 0;
  always @(posedge clk)
    if (!rst_n || !access || pready)
      f_wait <= 0;
    else
      f_wait <= f_wait + 1;

  generate if (MAXWAIT >= 0) begin : wait_bound
    always @(*)
      if (rst_n && access && f_wait >= MAXWAIT)
        assert(pready);
  end endgenerate

  // slave: PSLVERR only at the end of the access phase. low outside it is a
  // recommendation (par. 2.3)
  always @(*)
    if (rst_n && (!last || !SLVERR))
      assert(!pslverr);

  // the assumptions leave room for transfers
  always @(posedge clk)
    if (rst_n) begin
      cover(last && pwrite);
      cover(last && !pwrite);
      if (MAXWAIT != 0)
        cover(f_past_valid && last && $past(access) && !$past(pready));
    end

endmodule
