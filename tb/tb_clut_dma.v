// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// testbench for clut_dma.
// reads mem.hex (memory image) and tests.txt (one request per line:
// format tile_cols first w h fb_start map clut, hex; w and h in pixels), writes the received
// bytes to out.txt, in the format of expected.txt written by ref.c.
//
// parameter CACHE (iverilog -P tb_clut_dma.CACHE=0): the engine without
// cache, 3 memory reads per tile and pixel row.
//
// plusargs:
//   +ws=N   memory wait states, fixed. default: random 0..3
//   +spi=P  spi model: one tx data register, shift register P cycles per byte;
//           counts underruns: cycles the shift register is idle during a request.
//           default: random byte_ready, with bursts of stalls

module tb_clut_dma;

  parameter CACHE = 1;

  reg         clk = 0;
  reg         rst_n = 0;

  reg         psel = 0;
  reg         penable = 0;
  reg         pwrite = 0;
  reg  [4:0]  paddr = 0;
  reg  [31:0] pwdata = 0;
  wire [31:0] prdata;
  wire        pready;
  wire        pslverr;
  wire        irq;

  wire        rd_req;
  wire [31:0] rd_addr;
  reg         rd_ack = 0;
  reg  [31:0] rd_data = 0;

  wire        byte_valid;
  wire [7:0]  byte_data;
  wire        byte_ready;

  clut_dma #(.CACHE(CACHE)) dut (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .irq(irq),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready)
  );

  always #5 clk = ~clk;

  integer ws = -1;
  integer spi = 0;
  integer seed = 1;
  integer errors = 0;

  // memory, 64 kbyte. wait states, then rd_ack for one cycle
  reg [31:0] mem [0:16383];
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
  always @(posedge clk)
    if (byte_valid && byte_ready) begin
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

  // apb3 write and read
  task apb_wr(input [4:0] a, input [31:0] d);
    begin
      @(negedge clk);
      psel = 1; penable = 0; pwrite = 1; paddr = a; pwdata = d;
      @(negedge clk);
      penable = 1;
      @(negedge clk);
      psel = 0; penable = 0; pwrite = 0;
    end
  endtask

  task apb_rd(input [4:0] a, output [31:0] d);
    begin
      @(negedge clk);
      psel = 1; penable = 0; pwrite = 0; paddr = a;
      @(negedge clk);
      penable = 1;
      @(negedge clk);
      d = prdata;
      psel = 0; penable = 0;
    end
  endtask

  // memory reads, per request and in total
  integer req_reads = 0, total_reads = 0, exp_reads;
  always @(posedge clk)
    if (rd_ack) begin
      req_reads = req_reads + 1;
      total_reads = total_reads + 1;
    end

  integer tests, reads, r, t, cycles, limit;
  reg [31:0] fmt, tile_cols, first, w, h, fb, map, clut, status;

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
      apb_wr(5'h08, fb);
      apb_wr(5'h0C, map);
      apb_wr(5'h10, clut);
      apb_wr(5'h14, tile_cols - (w + 7) / 8);   // LINE_OFFSET
      apb_wr(5'h18, first);
      apb_wr(5'h1C, { 4'd0, h[11:0], 4'd0, w[11:0] });  // SIZE
      req_started = 0;
      req_bytes = 0;
      req_reads = 0;
      apb_wr(5'h00, { 28'd0, 1'b1, fmt[1:0], 1'b1 });   // IRQ_EN, FORMAT, START

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
      // memory reads, as computed by ref.c. without cache: 3 per tile and pixel row
      if (reads != 0 && $fscanf(reads, "%d\n", exp_reads) == 1) begin
        if (!CACHE)
          exp_reads = 3 * ((w + 7) / 8) * h;
        if (req_reads != exp_reads) begin
          $display("error: test %0d: %0d memory reads, expected %0d", t, req_reads, exp_reads);
          errors = errors + 1;
        end
      end
      apb_rd(5'h04, status);
      if (status[1:0] != 2'b10) begin
        $display("error: test %0d: status %b", t, status[1:0]);
        errors = errors + 1;
      end
      // registers the engine updates: FB_START steps per tile, FIRST per
      // tile row, H per pixel row
      if (w != 0 && h != 0) begin
        apb_rd(5'h08, status);
        if (status != fb + (h - 1) * tile_cols + (w + 7) / 8) begin
          $display("error: test %0d: FB_START %h at the end", t, status);
          errors = errors + 1;
        end
        apb_rd(5'h18, status);
        if (status != first + (h - 1) / 8 * tile_cols) begin
          $display("error: test %0d: FIRST %h at the end", t, status);
          errors = errors + 1;
        end
        apb_rd(5'h1C, status);
        if (status[27:16] != 0) begin
          $display("error: test %0d: H %0d at the end", t, status[27:16]);
          errors = errors + 1;
        end
      end
      apb_wr(5'h04, 32'h2);  // clear DONE
      @(negedge clk);
      if (irq) begin
        $display("error: test %0d: irq after clearing DONE", t);
        errors = errors + 1;
      end

      t = t + 1;
      r = $fscanf(tests, "%h %h %h %h %h %h %h %h\n", fmt, tile_cols, first, w, h, fb, map, clut);
    end

    $fclose(out);
    if (spi)
      $display("measure: spi %0d ws %0d bytes %0d reads %0d underrun %0d of %0d cycles, max gap %0d",
               spi, ws, nbytes, total_reads, underrun, req_cycles, max_gap);
    $display("%0d tests, %0d errors", t, errors);
    $finish;
  end

endmodule
