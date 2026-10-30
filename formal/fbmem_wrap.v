// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// fbmem against fahbl_slave, and its contents against a shadow copy of one
// word, at an address the solver chooses (anyconst):
//   - port A (AHB-Lite): a read returns what the last writes to the word put
//     in each byte lane;
//   - port B (clut_dma): rd_ack in the cycle after rd_req, and rd_data is the
//     word as it was in the cycle of rd_req: a port A write in that same cycle
//     is not seen yet.
// Bytes never written since power on are not checked: block ram has no reset.
// Lanes after the AMBA 3 AHB-Lite specification, ARM IHI 0033A, par. 6.1.1,
// table 6-1, little-endian.

module fbmem_wrap #(
  parameter AW = 3                // 2^AW words
) (
  input         clk,
  input         rst_n,
  input         hsel,
  input  [31:0] haddr,
  input  [1:0]  htrans,
  input         hwrite,
  input  [2:0]  hsize,
  input  [31:0] hwdata,
  input         hready,
  input         rd_req,
  input  [31:0] rd_addr
);

  wire        hreadyout, hresp, rd_ack;
  wire [31:0] hrdata, rd_data;

  fbmem #(.AW(AW)) dut (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data)
  );

  // reads have one wait state, writes none
  fahbl_slave #(.AW(32), .MAXWAIT(1)) ahb (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp)
  );

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // active byte lanes, little-endian (par. 6.1.1, table 6-1)
  function [3:0] f_lanes(input [2:0] size, input [1:0] a);
    case (size)
      3'd0:    f_lanes = (a == 2'd0) ? 4'b0001 : (a == 2'd1) ? 4'b0010 :
                         (a == 2'd2) ? 4'b0100 : 4'b1000;
      3'd1:    f_lanes = a[1] ? 4'b1100 : 4'b0011;
      default: f_lanes = 4'b1111;
    endcase
  endfunction

  // port A data phase, from the address phase sampled with HREADY high
  reg          f_dx = 0, f_dw = 0;
  reg [AW-1:0] f_dword = 0;
  reg [3:0]    f_dbe = 0;
  always @(posedge clk)
    if (!rst_n)
      f_dx <= 0;
    else if (hready) begin
      f_dx    <= hsel && htrans[1];
      f_dw    <= hwrite;
      f_dword <= haddr[AW+1:2];
      f_dbe   <= f_lanes(hsize, haddr[1:0]);
    end

  // the shadow word
  (* anyconst *) reg [AW-1:0] f_addr;
  reg [31:0] f_mem = 0;
  reg [3:0]  f_known = 0;           // lanes written since power on

  wire f_wr = rst_n && f_dx && f_dw && hready && f_dword == f_addr;
  wire f_rd = rst_n && f_dx && !f_dw && hreadyout && f_dword == f_addr;

  genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : lane
    always @(posedge clk)
      if (f_wr && f_dbe[i]) begin
        f_mem[8*i +: 8] <= hwdata[8*i +: 8];
        f_known[i]      <= 1;
      end

    // port A: the last cycle of a read
    always @(*)
      if (f_rd && f_known[i])
        assert(hrdata[8*i +: 8] == f_mem[8*i +: 8]);

    // port B: the word as it was in the cycle of the request
    always @(posedge clk)
      if (f_past_valid && rd_ack && $past(rd_addr[AW+1:2]) == f_addr && $past(f_known[i]))
        assert(rd_data[8*i +: 8] == $past(f_mem[8*i +: 8]));
  end endgenerate

  // port B: rd_ack in the cycle after rd_req
  always @(posedge clk)
    if (f_past_valid && rst_n && $past(rst_n))
      assert(rd_ack == $past(rd_req && !rd_ack));

  // reads of written data, on both ports
  always @(posedge clk)
    if (f_past_valid && rst_n) begin
      cover(f_rd && f_known == 4'hF);
      cover(rd_ack && $past(rd_addr[AW+1:2]) == f_addr && $past(f_known) == 4'hF);
      // read address phase in the data phase of a write: read after write
      cover(f_rd && $past(f_wr, 2));
    end

endmodule
