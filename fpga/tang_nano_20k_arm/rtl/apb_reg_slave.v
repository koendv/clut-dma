// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// apb_reg_slave: APB3 slave for the register interface of clut_dma. Wiring
// only: no wait states (PREADY high), no errors (PSLVERR low).
//   write: reg_wr in the access phase, the register written in that cycle
//   read:  reg_rd in the setup phase; reg_rdata is valid in the next cycle,
//          the access phase, and goes to PRDATA
// Byte offsets 0x00..0x1F, word registers: paddr[1:0] and the byte lanes
// are not used (APB3 has no PSTRB).

module apb_reg_slave (
  input         psel,
  input         penable,
  input         pwrite,
  input  [4:0]  paddr,
  input  [31:0] pwdata,
  output [31:0] prdata,
  output        pready,
  output        pslverr,

  output        reg_wr,
  output        reg_rd,
  output [4:2]  reg_addr,
  output [31:0] reg_wdata,
  input  [31:0] reg_rdata
);

  wire unused = &{ 1'b0, paddr[1:0] };

  assign reg_wr    = psel && penable && pwrite;
  assign reg_rd    = psel && !penable && !pwrite;
  assign reg_addr  = paddr[4:2];
  assign reg_wdata = pwdata;
  assign prdata    = reg_rdata;
  assign pready    = 1'b1;
  assign pslverr   = 1'b0;

endmodule
