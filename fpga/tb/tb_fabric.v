// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// testbench for clut_fabric: clut_dma, fbmem and spi_lcd, as the cpu uses them.
// reads mem.hex (16 kbyte memory image) and tests.txt (one request per line:
// format tile_cols first w h fb_start, hex; w and h in pixels), both from
// tb/ref.c "fabric".
//   - loads the image with a pipelined AHB master: random byte, halfword and
//     word writes, random reads in between, also of the word just written,
//     and idle cycles. then reads back all of the memory.
//   - tests the gpio register.
//   - per request: display window commands through spi_lcd DATA, then
//     clut_dma streams the pixels through spi_lcd.
// writes the spi bytes with d/c to out.txt, in the format of expected.txt.

module tb_fabric;

  reg         clk = 0;
  reg         rst_n = 0;
  always #5 clk = ~clk;

  // APB
  reg         psel = 0, penable = 0, pwrite = 0;
  reg  [11:0] paddr = 0;
  reg  [31:0] pwdata = 0;
  wire [31:0] prdata;
  wire        pready, pslverr;

  // AHB, fbmem is the only slave
  reg         hsel = 0, hwrite = 0;
  reg  [31:0] haddr = 0, hwdata = 0;
  reg  [1:0]  htrans = 0;
  reg  [2:0]  hsize = 0;
  wire        hreadyout, hresp;
  wire [31:0] hrdata;
  wire        hready = hreadyout;

  wire        irq;
  wire [1:0]  gpio_out;
  reg         gpio_in = 0;
  wire        sck, mosi, dc, cs_n, lcd_rst_n, bl;

  clut_fabric dut (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .irq(irq),
    .gpio_out(gpio_out), .gpio_in(gpio_in),
    .lcd_sck(sck), .lcd_mosi(mosi), .lcd_dc(dc), .lcd_cs_n(cs_n),
    .lcd_rst_n(lcd_rst_n), .lcd_bl(bl)
  );

  integer errors = 0;
  integer seed = 1;

  // ------------------------------------------------------------------
  // AHB master: a queue of transfers, run pipelined

  localparam MEM_BASE = 32'h80000000;
  localparam QN = 32768;
  localparam K_IDLE = 0, K_WRITE = 1, K_READ = 2;

  reg [1:0]  q_kind [0:QN-1];
  reg [13:0] q_addr [0:QN-1];
  reg [2:0]  q_size [0:QN-1];
  reg [31:0] q_data [0:QN-1];   // write: data on its byte lanes. read: expected word
  integer    qn;

  reg [7:0]  model [0:16383];   // memory contents, as the cpu wrote them
  reg [31:0] image [0:4095];

  function [31:0] model_word(input [13:0] a);
    model_word = { model[{a[13:2], 2'd3}], model[{a[13:2], 2'd2}],
                   model[{a[13:2], 2'd1}], model[{a[13:2], 2'd0}] };
  endfunction

  task q_idle;
    begin
      q_kind[qn] = K_IDLE;
      qn = qn + 1;
    end
  endtask

  // write size 0, 1 or 2 bytes (1, 2, 4) of word w at byte address a
  task q_write(input [13:0] a, input [2:0] size, input [31:0] w);
    integer i;
    begin
      q_kind[qn] = K_WRITE;
      q_addr[qn] = a;
      q_size[qn] = size;
      q_data[qn] = w;
      for (i = 0; i < 4; i = i + 1)
        if ((size == 2) || (size == 1 && a[1] == i[1]) || (size == 0 && a[1:0] == i[1:0]))
          model[{a[13:2], i[1:0]}] = w[8*i +: 8];
      qn = qn + 1;
    end
  endtask

  task q_read(input [13:0] a);
    begin
      q_kind[qn] = K_READ;
      q_addr[qn] = { a[13:2], 2'b00 };
      q_size[qn] = 2;
      q_data[qn] = model_word(a);
      qn = qn + 1;
    end
  endtask

  task drive_addr(input integer i);
    begin
      if (i < qn && q_kind[i] != K_IDLE) begin
        hsel   = 1;
        htrans = 2'b10;   // NONSEQ
        haddr  = MEM_BASE | q_addr[i];
        hwrite = q_kind[i] == K_WRITE;
        hsize  = q_size[i];
      end else begin
        hsel   = 0;
        htrans = 2'b00;
        hwrite = 0;
      end
    end
  endtask

  task ahb_run;
    integer a_i, d_i;   // transfer in the address phase, in the data phase
    begin
      a_i = 0;
      d_i = -1;
      @(negedge clk);
      drive_addr(a_i);
      while (a_i < qn || d_i >= 0) begin
        @(posedge clk);
        if (hready) begin
          if (d_i >= 0 && q_kind[d_i] == K_READ && hrdata !== q_data[d_i]) begin
            $display("error: ahb read %h: %h, expected %h", q_addr[d_i], hrdata, q_data[d_i]);
            errors = errors + 1;
          end
          d_i = (a_i < qn) ? a_i : -1;
          if (a_i < qn)
            a_i = a_i + 1;
          @(negedge clk);
          drive_addr(a_i);
          hwdata = (d_i >= 0 && q_kind[d_i] == K_WRITE) ? q_data[d_i] : 32'hdeadbeef;
        end
      end
      @(negedge clk);
      drive_addr(qn);
    end
  endtask

  // ------------------------------------------------------------------
  // APB master

  task apb_wr(input [11:0] a, input [31:0] d);
    begin
      @(negedge clk);
      psel = 1; penable = 0; pwrite = 1; paddr = a; pwdata = d;
      @(negedge clk);
      penable = 1;
      @(posedge clk);
      while (!pready)
        @(posedge clk);
      @(negedge clk);
      psel = 0; penable = 0; pwrite = 0;
    end
  endtask

  task apb_rd(input [11:0] a, output [31:0] d);
    begin
      @(negedge clk);
      psel = 1; penable = 0; pwrite = 0; paddr = a;
      @(negedge clk);
      penable = 1;
      @(posedge clk);
      while (!pready)
        @(posedge clk);
      d = prdata;
      @(negedge clk);
      psel = 0; penable = 0;
    end
  endtask

  // ------------------------------------------------------------------
  // spi_lcd

  localparam SPI_DATA = 12'h040, SPI_CTRL = 12'h044, SPI_STATUS = 12'h048, SPI_DIV = 12'h04C;
  localparam C_DC = 1, C_CS_N = 2, C_RST_N = 4, C_BL = 8, C_STREAM = 16;
  localparam C_ON = C_RST_N | C_BL;   // chip select low, out of reset, backlight on

  task spi_wait;
    reg [31:0] s;
    begin
      s = 1;
      while (s[0])
        apb_rd(SPI_STATUS, s);
    end
  endtask

  task spi_cmd(input [7:0] c);
    begin
      spi_wait;
      apb_wr(SPI_CTRL, C_ON);
      apb_wr(SPI_DATA, c);
      spi_wait;
      apb_wr(SPI_CTRL, C_ON | C_DC);
    end
  endtask

  task spi_data16(input [15:0] d);
    begin
      apb_wr(SPI_DATA, d[15:8]);
      apb_wr(SPI_DATA, d[7:0]);
    end
  endtask

  // spi receiver: mode 0, msb first
  integer   out;
  reg [7:0] rx;
  integer   nbits = 0;
  always @(posedge sck)
    if (!cs_n) begin
      rx = { rx[6:0], mosi };
      nbits = nbits + 1;
      if (nbits == 8) begin
        $fwrite(out, "%s %02x\n", dc ? "d" : "c", rx);
        nbits = 0;
      end
    end

  // while clut_dma streams, the spi runs without pauses: with DIV 1 a rising
  // sck edge every 2 clock cycles, from the first pixel byte to the last
  reg  streaming = 0;
  time last_edge = 0;
  integer gaps = 0;
  always @(posedge sck)
    if (streaming) begin
      if (last_edge != 0 && $time - last_edge != 20) begin
        if (gaps < 5)
          $display("error: spi pause of %0t at %0t", $time - last_edge - 20, $time);
        gaps = gaps + 1;
      end
      last_edge = $time;
    end

  // ------------------------------------------------------------------

  localparam FB = 32'h80000000, MAP = 32'h80002100, CLUT = 32'h80002340;
  localparam YOFS = 20;

  integer    i, k, r, t, tests, cycles;
  reg [31:0] fmt, tile_cols, first, w, h, fb_start, x0, x1, y0, y1, d, junk;

  initial begin
    $readmemh("mem.hex", image);
    tests = $fopen("tests.txt", "r");
    out = $fopen("out.txt", "w");
    if (tests == 0 || out == 0) begin
      $display("error: cannot open tests.txt or out.txt");
      $finish;
    end
    for (i = 0; i < 16384; i = i + 1)
      model[i] = 0;

    repeat (3) @(negedge clk);
    rst_n = 1;

    // load the image: random sizes, random reads and idle cycles in between
    qn = 0;
    for (k = 0; k < 4096; k = k + 1) begin
      // random data on the byte lanes that are not written
      junk = $random(seed);
      case ($unsigned($random(seed)) % 3)
        0: q_write(4 * k, 2, image[k]);
        1: begin
             q_write(4 * k,     1, { junk[31:16], image[k][15:0] });
             q_write(4 * k + 2, 1, { image[k][31:16], junk[15:0] });
           end
        default: begin
             q_write(4 * k,     0, { junk[31:8], image[k][7:0] });
             q_write(4 * k + 1, 0, { junk[31:16], image[k][15:8], junk[7:0] });
             q_write(4 * k + 2, 0, { junk[31:24], image[k][23:16], junk[15:0] });
             q_write(4 * k + 3, 0, { image[k][31:24], junk[23:0] });
           end
      endcase
      r = $unsigned($random(seed)) % 8;
      if (r == 0)
        q_read(4 * k);                                     // the word just written
      else if (r == 1)
        q_read(4 * ($unsigned($random(seed)) % (k + 1)));  // an earlier word
      else if (r == 2)
        q_idle;
    end
    ahb_run;

    // read back everything
    qn = 0;
    for (k = 0; k < 4096; k = k + 1)
      q_read(4 * k);
    ahb_run;
    $display("ahb: image loaded and read back");

    // gpio
    apb_wr(12'h080, 32'h3);
    apb_rd(12'h080, d);
    if (d != 32'h3 || gpio_out != 2'b11) begin
      $display("error: gpio out %h", d);
      errors = errors + 1;
    end
    gpio_in = 1;
    repeat (4) @(negedge clk);
    apb_rd(12'h080, d);
    if (d != 32'h103) begin
      $display("error: gpio in %h", d);
      errors = errors + 1;
    end
    apb_wr(12'h080, 32'h0);

    // display: fast spi, chip select low
    apb_wr(SPI_DIV, 1);
    apb_wr(SPI_CTRL, C_ON | C_DC);

    // clut_dma memory
    apb_wr(12'h008, FB);    // FB_START, again per request
    apb_wr(12'h00C, MAP);
    apb_wr(12'h010, CLUT);

    t = 0;
    r = $fscanf(tests, "%h %h %h %h %h %h\n", fmt, tile_cols, first, w, h, fb_start);
    while (r == 6) begin
      // display window, as on an st7789 240x280
      x0 = 8 * (first % tile_cols);
      x1 = x0 + w - 1;
      y0 = 8 * (first / tile_cols) + YOFS;
      y1 = y0 + h - 1;
      spi_cmd(8'h2a);
      spi_data16(x0);
      spi_data16(x1);
      spi_cmd(8'h2b);
      spi_data16(y0);
      spi_data16(y1);
      spi_cmd(8'h2c);

      // pixels from clut_dma
      spi_wait;
      apb_wr(SPI_CTRL, C_ON | C_DC | C_STREAM);
      apb_wr(12'h008, FB + fb_start);
      apb_wr(12'h014, tile_cols - (w + 7) / 8);   // LINE_OFFSET
      apb_wr(12'h018, first);
      apb_wr(12'h01C, { 4'd0, h[11:0], 4'd0, w[11:0] });
      last_edge = 0;
      streaming = 1;
      apb_wr(12'h000, { 28'd0, 1'b1, fmt[1:0], 1'b1 });   // IRQ_EN, FORMAT, START

      cycles = 0;
      while (!irq && cycles < 4000000) begin
        @(negedge clk);
        cycles = cycles + 1;
      end
      if (!irq) begin
        $display("error: test %0d: timeout", t);
        $finish;
      end
      apb_wr(12'h004, 32'h2);     // clear DONE
      spi_wait;
      streaming = 0;
      apb_wr(SPI_CTRL, C_ON | C_DC);

      t = t + 1;
      r = $fscanf(tests, "%h %h %h %h %h %h\n", fmt, tile_cols, first, w, h, fb_start);
    end

    repeat (20) @(negedge clk);
    $fclose(out);
    if (gaps != 0) begin
      $display("error: %0d spi pauses while streaming", gaps);
      errors = errors + 1;
    end
    $display("%0d requests, %0d errors", t, errors);
    $finish;
  end

endmodule
