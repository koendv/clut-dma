// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// testbench for clut_dma.
// reads mem.hex (memory image) and tests.txt (one request per line:
// format tile_cols first w h fb_start map clut, hex; w and h in pixels), writes the received
// bytes to out.txt, in the format of expected.txt written by ref.c.
// format bits 1:0 FORMAT, bit 2 the vertical layout (CTRL bit 4), bit 3 NO_MAP
// (CTRL bit 5).
//
// parameter CACHE (iverilog -P tb_clut_dma.CACHE=0): the engine without
// cache, 3 memory reads per tile and pixel row; vertical: 4.
//
// plusargs:
//   +ws=N   memory wait states, fixed. default: random 0..3
//   +spi=P  spi model: one tx data register, shift register P cycles per byte;
//           counts underruns: cycles the shift register is idle during a request.
//           default: random byte_ready, with bursts of stalls
//
// define AHB (iverilog -DAHB): clut_dma behind the AHB-Lite adapters of the
// GoRV32 build, ahb_reg_slave for the registers and ahb_rd_master for the
// memory, an AHB-Lite slave with the same wait states. Reads above the 64
// kbyte memory get an ERROR response; a last test checks ERR.

module tb_clut_dma;

  parameter CACHE = 1;

  reg         clk = 0;
  reg         rst_n = 0;

  wire        irq;
  wire        rd_req;
  wire [31:0] rd_addr;
  wire        byte_valid;
  wire [7:0]  byte_data;
  wire        byte_ready;

`ifndef AHB
  reg         reg_wr = 0;
  reg         reg_rd = 0;
  reg  [4:2]  reg_addr = 0;
  reg  [31:0] reg_wdata = 0;
  wire [31:0] reg_rdata;

  reg         rd_ack = 0;
  reg  [31:0] rd_data = 0;

  clut_dma #(.CACHE(CACHE)) dut (
    .clk(clk), .rst_n(rst_n),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata),
    .irq(irq),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data), .rd_err(1'b0),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready)
  );
`else
  // registers: the testbench is the AHB master, ahb_reg_slave the only slave
  reg         s_hsel = 0;
  reg  [31:0] s_haddr = 0;
  reg  [1:0]  s_htrans = 0;
  reg         s_hwrite = 0;
  reg  [2:0]  s_hsize = 3'd2;
  reg  [31:0] s_hwdata = 0;
  wire        s_hreadyout, s_hresp;
  wire [31:0] s_hrdata;

  wire        reg_wr, reg_rd;
  wire [4:2]  reg_addr;
  wire [31:0] reg_wdata, reg_rdata;

  ahb_reg_slave regs (
    .clk(clk), .rst_n(rst_n),
    .hsel(s_hsel), .haddr(s_haddr), .htrans(s_htrans), .hwrite(s_hwrite),
    .hsize(s_hsize), .hwdata(s_hwdata), .hready(s_hreadyout),
    .hreadyout(s_hreadyout), .hrdata(s_hrdata), .hresp(s_hresp),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata)
  );

  // memory: ahb_rd_master, and the memory model below the only slave
  wire        rd_ack, rd_err;
  wire [31:0] rd_data;
  wire [31:0] m_haddr, m_hwdata;
  wire [1:0]  m_htrans;
  wire        m_hwrite;
  wire [2:0]  m_hsize, m_hburst;
  wire [3:0]  m_hprot;
  reg         m_hready = 1;
  reg  [31:0] m_hrdata = 0;
  reg         m_hresp = 0;

  ahb_rd_master rdm (
    .clk(clk), .rst_n(rst_n),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack),
    .rd_data(rd_data), .rd_err(rd_err),
    .haddr(m_haddr), .htrans(m_htrans), .hwrite(m_hwrite), .hsize(m_hsize),
    .hburst(m_hburst), .hprot(m_hprot), .hwdata(m_hwdata),
    .hready(m_hready), .hrdata(m_hrdata), .hresp(m_hresp)
  );

  clut_dma #(.CACHE(CACHE)) dut (
    .clk(clk), .rst_n(rst_n),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata),
    .irq(irq),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data), .rd_err(rd_err),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready)
  );
`endif

  always #5 clk = ~clk;

  integer ws = -1;
  integer spi = 0;
  integer seed = 1;
  integer errors = 0;

  // memory, 64 kbyte
  reg [31:0] mem [0:16383];
`ifndef AHB
  // wait states, then rd_ack for one cycle
  integer    wait_cnt = 0;
  reg [31:0] req_addr;
  reg        req_pending = 0;
  always @(posedge clk) begin
    rd_ack <= 0;
    if (rd_req && !rd_ack) begin
      if (req_pending && rd_addr != req_addr) begin
        $display("error: rd_addr changed during request");
        errors = errors + 1;
      end
      if (rd_addr[1:0] != 0) begin
        $display("error: unaligned read %h", rd_addr);
        errors = errors + 1;
      end
      req_pending <= 1;
      req_addr    <= rd_addr;
      if (wait_cnt == 0) begin
        rd_ack      <= 1;
        rd_data     <= mem[rd_addr[15:2]];
        req_pending <= 0;
        wait_cnt    = (ws >= 0) ? ws : $unsigned($random(seed)) % 4;
      end else
        wait_cnt = wait_cnt - 1;
    end
  end
`else
  // AHB-Lite slave: a read gets the wait states, then the data. above the
  // memory: ERROR, two cycles
  integer    m_wait = 0;
  reg [31:0] m_addr = 0;
  always @(posedge clk)
    if (m_hready) begin
      m_hresp <= 0;
      if (m_htrans[1]) begin
        if (m_hwrite || m_hsize != 3'd2 || m_haddr[1:0] != 0 || m_hburst != 0) begin
          $display("error: not a single word read: %h", m_haddr);
          errors = errors + 1;
        end
        m_addr = m_haddr;
        m_wait = (ws >= 0) ? ws : $unsigned($random(seed)) % 4;
        if (m_haddr >= 32'h10000) begin
          m_hready <= 0;
          m_hresp  <= 1;
        end else if (m_wait == 0)
          m_hrdata <= mem[m_haddr[15:2]];
        else
          m_hready <= 0;
      end
    end else if (m_hresp)
      m_hready <= 1;            // second cycle of ERROR
    else begin
      m_wait = m_wait - 1;
      if (m_wait == 0) begin
        m_hready <= 1;
        m_hrdata <= mem[m_addr[15:2]];
      end
    end
`endif

  // random byte_ready: about 3 in 4 cycles, now and then a stall of 20..59 cycles
  reg     rnd_ready = 0;
  integer stall = 0;
  always @(posedge clk)
    if (stall > 0) begin
      stall = stall - 1;
      rnd_ready <= 0;
    end else if ($unsigned($random(seed)) % 64 == 0) begin
      stall = 20 + $unsigned($random(seed)) % 40;
      rnd_ready <= 0;
    end else
      rnd_ready <= ($unsigned($random(seed)) % 4) != 0;

  // spi model: tx data register and shift register
  reg     dr_full = 0;
  integer shift_cnt = 0;
  reg     load;
  reg     req_started = 0;
  integer req_bytes = 0;      // bytes of this request taken by the spi
  integer underrun = 0, req_cycles = 0, gap = 0, max_gap = 0;
  wire    counting = req_started && dut.busy && dut.state != 4'd8;  // not in S_DRAIN

  assign byte_ready = spi ? !dr_full : rnd_ready;

  always @(posedge clk)
    if (spi) begin
      load = dr_full && shift_cnt <= 1;
      if (byte_valid && byte_ready)
        dr_full <= 1;
      else if (load)
        dr_full <= 0;
      if (load) begin
        shift_cnt <= spi;
        // a byte of this request, not the last byte of the previous one
        if (dut.busy && req_bytes > 0)
          req_started <= 1;
      end else if (shift_cnt != 0)
        shift_cnt <= shift_cnt - 1;
      if (counting) begin
        req_cycles = req_cycles + 1;
        if (shift_cnt == 0) begin
          underrun = underrun + 1;
          gap = gap + 1;
          if (gap > max_gap)
            max_gap = gap;
        end else
          gap = 0;
      end
    end

  // received bytes
  integer out;
  integer nbytes = 0;
  reg     logging = 1;
  always @(posedge clk)
    if (byte_valid && byte_ready) begin
      if (logging)
        $fwrite(out, "%02x\n", byte_data);
      nbytes = nbytes + 1;
      req_bytes = req_bytes + 1;
    end

  // the last byte has left the buffer when the interrupt comes
  reg irq_d = 0;
  always @(posedge clk) begin
    irq_d <= irq;
    if (irq && !irq_d && (byte_valid || dut.skid_valid)) begin
      $display("error: irq with bytes in the buffer");
      errors = errors + 1;
    end
  end

`ifndef AHB
  // register write and read, a at the byte offset
  task wr_reg(input [4:0] a, input [31:0] d);
    begin
      @(negedge clk);
      reg_wr = 1; reg_addr = a[4:2]; reg_wdata = d;
      @(negedge clk);
      reg_wr = 0;
    end
  endtask

  task rd_reg(input [4:0] a, output [31:0] d);
    begin
      @(negedge clk);
      reg_rd = 1; reg_addr = a[4:2];
      @(negedge clk);
      reg_rd = 0;
      d = reg_rdata;
    end
  endtask
`else
  // register write and read over AHB-Lite, a at the byte offset: the address
  // phase, then the data phase until HREADYOUT; the task returns after it
  task wr_reg(input [4:0] a, input [31:0] d);
    begin
      @(negedge clk);
      s_hsel = 1; s_htrans = 2'b10; s_hwrite = 1; s_haddr = { 27'd0, a };
      @(negedge clk);
      s_hsel = 0; s_htrans = 2'b00; s_hwdata = d;
      while (!s_hreadyout)
        @(negedge clk);
      if (s_hresp) begin
        $display("error: ERROR response to a register write");
        errors = errors + 1;
      end
      @(negedge clk);           // the data phase ends at this posedge
    end
  endtask

  task rd_reg(input [4:0] a, output [31:0] d);
    begin
      @(negedge clk);
      s_hsel = 1; s_htrans = 2'b10; s_hwrite = 0; s_haddr = { 27'd0, a };
      @(negedge clk);
      s_hsel = 0; s_htrans = 2'b00;
      while (!s_hreadyout)
        @(negedge clk);
      if (s_hresp) begin
        $display("error: ERROR response to a register read");
        errors = errors + 1;
      end
      d = s_hrdata;
      @(negedge clk);           // the data phase ends at this posedge
    end
  endtask
`endif

  // memory reads, per request and in total
  integer req_reads = 0, total_reads = 0, exp_reads;
  always @(posedge clk)
    if (rd_ack) begin
      req_reads = req_reads + 1;
      total_reads = total_reads + 1;
    end

  integer tests, reads, r, t, cycles, limit;
  reg [31:0] fmt, tile_cols, first, w, h, fb, map, clut, status, nib;

  initial begin
    if (!$value$plusargs("ws=%d", ws))
      ws = -1;
    if (!$value$plusargs("spi=%d", spi))
      spi = 0;
    $readmemh("mem.hex", mem);
    tests = $fopen("tests.txt", "r");
    reads = $fopen("reads.txt", "r");
    out = $fopen("out.txt", "w");
    if (tests == 0 || out == 0) begin
      $display("error: cannot open tests.txt or out.txt");
      $finish;
    end

    repeat (3) @(negedge clk);
    rst_n = 1;

    t = 0;
    r = $fscanf(tests, "%h %h %h %h %h %h %h %h\n", fmt, tile_cols, first, w, h, fb, map, clut);
    while (r == 8) begin
      $fwrite(out, "# %0d %0d %0d %0d %0d %0d\n", t, fmt, tile_cols, first, w, h);
      // FB_START the first tile, MAP_START and MAP_NIBBLE its map entry,
      // LINE_STRIDE a pixel row (horizontal) or a tile row (vertical)
      wr_reg(5'h08, fb);
      wr_reg(5'h0C, map + first / 2);
      wr_reg(5'h10, clut);
      wr_reg(5'h14, fmt[2] ? 8 * tile_cols : tile_cols);   // LINE_STRIDE
      wr_reg(5'h18, first & 1);                            // MAP_NIBBLE
      wr_reg(5'h1C, { 4'd0, h[11:0], 4'd0, w[11:0] });     // SIZE
      req_started = 0;
      req_bytes = 0;
      req_reads = 0;
      wr_reg(5'h00, { 26'd0, fmt[3:2], 1'b1, fmt[1:0], 1'b1 });   // NO_MAP, VERT, IRQ_EN, FORMAT, START

      // wait for the interrupt. 64 pixels per tile, at most 3 bytes per pixel
      limit = w * h * (3 * (spi ? spi : 8) + 16) + 2000;   // at most 3 bytes per pixel
      cycles = 0;
      while (!irq && cycles < limit) begin
        @(negedge clk);
        cycles = cycles + 1;
      end
      if (!irq) begin
        $display("error: test %0d: timeout", t);
        $finish;
      end
      // memory reads, as computed by ref.c. without cache: 3 per tile and
      // pixel row; vertical: map, clut and 2 or 3 bitmap words
      if (reads != 0 && $fscanf(reads, "%d\n", exp_reads) == 1) begin
        if (!CACHE)
          exp_reads = ((fmt[2] ? 4 : 3) - fmt[3]) * ((w + 7) / 8) * h;
        if (req_reads != exp_reads) begin
          $display("error: test %0d: %0d memory reads, expected %0d", t, req_reads, exp_reads);
          errors = errors + 1;
        end
      end
      rd_reg(5'h04, status);
      if (status[1:0] != 2'b10) begin
        $display("error: test %0d: status %b", t, status[1:0]);
        errors = errors + 1;
      end
      // registers the engine updates: FB_START per line (a pixel row, or a
      // tile row vertical), MAP_START and MAP_NIBBLE per tile row, H per
      // pixel row. at the end: the start of the last line
      if (w != 0 && h != 0) begin
        rd_reg(5'h08, status);
        if (status != (fmt[2] ? fb + 8 * ((h - 1) / 8) * tile_cols : fb + (h - 1) * tile_cols)) begin
          $display("error: test %0d: FB_START %h at the end", t, status);
          errors = errors + 1;
        end
        rd_reg(5'h0C, status);
        rd_reg(5'h18, nib);
        if ({ status, nib[0] } != 2 * map + first + (h - 1) / 8 * tile_cols) begin
          $display("error: test %0d: MAP_START %h nibble %0d at the end", t, status, nib[0]);
          errors = errors + 1;
        end
        rd_reg(5'h1C, status);
        if (status[27:16] != 0) begin
          $display("error: test %0d: H %0d at the end", t, status[27:16]);
          errors = errors + 1;
        end
      end
      wr_reg(5'h04, 32'h2);  // clear DONE
      @(negedge clk);
      if (irq) begin
        $display("error: test %0d: irq after clearing DONE", t);
        errors = errors + 1;
      end

      t = t + 1;
      r = $fscanf(tests, "%h %h %h %h %h %h %h %h\n", fmt, tile_cols, first, w, h, fb, map, clut);
    end

`ifdef AHB
    // bus error: a rectangle 16 x 8, horizontal, no map. pixel row 0 is the
    // last 16 bytes of the memory, row 1 above it: its first bitmap read gets
    // ERROR. the 32 bytes of row 0 still go out, then ERR, not DONE; the
    // interrupt until ERR is cleared
    logging = 0;
    wr_reg(5'h08, 32'hfff0);                  // FB_START
    wr_reg(5'h10, 32'h0);                     // CLUT
    wr_reg(5'h14, 32'h10);                    // LINE_STRIDE
    wr_reg(5'h1C, { 4'd0, 12'd8, 4'd0, 12'd16 });   // SIZE, 16 x 8
    nib = nbytes;
    wr_reg(5'h00, 32'h29);                    // NO_MAP, IRQ_EN, rgb565, START
    cycles = 0;
    while (!irq && cycles < 10000) begin
      @(negedge clk);
      cycles = cycles + 1;
    end
    rd_reg(5'h04, status);
    if (!irq || status[2:0] != 3'b100) begin
      $display("error: bus error: irq %b status %b", irq, status[2:0]);
      errors = errors + 1;
    end
    if (nbytes - nib != 32) begin
      $display("error: bus error: %0d bytes sent, expected 32", nbytes - nib);
      errors = errors + 1;
    end
    wr_reg(5'h04, 32'h4);                     // clear ERR
    @(negedge clk);
    rd_reg(5'h04, status);
    if (irq || status[2:0] != 3'b000) begin
      $display("error: bus error: after clearing ERR irq %b status %b", irq, status[2:0]);
      errors = errors + 1;
    end
    // a byte transfer to the registers: ERROR
    @(negedge clk);
    s_hsel = 1; s_htrans = 2'b10; s_hwrite = 1; s_hsize = 3'd0; s_haddr = 32'h0;
    @(negedge clk);
    s_hsel = 0; s_htrans = 2'b00; s_hsize = 3'd2; s_hwdata = 32'h1;
    if (!s_hresp || s_hreadyout) begin
      $display("error: byte write: no ERROR response");
      errors = errors + 1;
    end
    @(negedge clk);
    if (!s_hresp || !s_hreadyout || dut.busy) begin
      $display("error: byte write: ERROR not two cycles, or it started the engine");
      errors = errors + 1;
    end
    t = t + 1;
`endif

    $fclose(out);
    if (spi)
      $display("measure: spi %0d ws %0d bytes %0d reads %0d underrun %0d of %0d cycles, max gap %0d",
               spi, ws, nbytes, total_reads, underrun, req_cycles, max_gap);
    $display("%0d tests, %0d errors", t, errors);
    $finish;
  end

endmodule
