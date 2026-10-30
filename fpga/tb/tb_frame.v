// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// tb_frame: the spi bytes clut_fabric sends for a firmware test, for the image
// the display ought to show. Input from fpga/tang_nano_20k/fw/host.c:
//   mem.hex    fbmem, loaded directly into the block ram
//   steps.txt  c XX            command byte, chip select high before it
//              d XX            parameter byte
//              w x0 x1 y0 y1   display window: CASET, RASET, RAMWR
//              m word value    fbmem write, directly into the block ram
//              f               end of a frame: an "f" line in spi.txt
//              r fmt fb_start map clut line_offset first size
//                              clut_dma request, streamed through spi_lcd
// as the firmware does it, with the spi at clk / 2.
// Writes spi.txt, "c xx" or "d xx" per byte, for fpga/tb/st7789.py.

module tb_frame;

  reg         clk = 0;
  reg         rst_n = 0;
  always #5 clk = ~clk;

  reg         psel = 0, penable = 0, pwrite = 0;
  reg  [11:0] paddr = 0;
  reg  [31:0] pwdata = 0;
  wire [31:0] prdata;
  wire        pready, pslverr;
  wire        hreadyout, hresp, irq;
  wire [31:0] hrdata;
  wire [1:0]  gpio_out;
  wire        sck, mosi, dc, cs_n, lcd_rst_n, bl;

  // no AHB master: the memory is loaded directly
  clut_fabric dut (
    .clk(clk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .hsel(1'b0), .haddr(32'd0), .htrans(2'd0), .hwrite(1'b0),
    .hsize(3'd0), .hwdata(32'd0), .hready(1'b1),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .irq(irq),
    .gpio_out(gpio_out), .gpio_in(1'b0),
    .lcd_sck(sck), .lcd_mosi(mosi), .lcd_dc(dc), .lcd_cs_n(cs_n),
    .lcd_rst_n(lcd_rst_n), .lcd_bl(bl)
  );

  localparam SPI_DATA = 12'h040, SPI_CTRL = 12'h044, SPI_STATUS = 12'h048, SPI_DIV = 12'h04C;
  localparam C_DC = 1, C_CS_N = 2, C_STREAM = 16;
  localparam ON = 4 | 8;                    // RST_N, BL

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
      @(negedge clk);
      d = prdata;
      psel = 0; penable = 0;
    end
  endtask

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
      apb_wr(SPI_CTRL, ON | C_CS_N);
      apb_wr(SPI_CTRL, ON);
      apb_wr(SPI_DATA, c);
      spi_wait;
      apb_wr(SPI_CTRL, ON | C_DC);
    end
  endtask

  task data16(input [15:0] v);
    begin
      apb_wr(SPI_DATA, v[15:8]);
      apb_wr(SPI_DATA, v[7:0]);
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
  always @(posedge cs_n)
    nbits = 0;

  reg [31:0] image [0:4095];
  integer    f, r, i, nreq;
  reg [7:0]  k;
  reg [31:0] a0, a1, a2, a3, a4, a5, a6, s;

  initial begin
    $readmemh("mem.hex", image);
    for (i = 0; i < 4096; i = i + 1) begin
      dut.mem.mem0[i] = image[i][7:0];
      dut.mem.mem1[i] = image[i][15:8];
      dut.mem.mem2[i] = image[i][23:16];
      dut.mem.mem3[i] = image[i][31:24];
    end
    f = $fopen("steps.txt", "r");
    out = $fopen("spi.txt", "w");
    if (f == 0 || out == 0) begin
      $display("error: cannot open steps.txt or spi.txt");
      $finish;
    end

    repeat (3) @(negedge clk);
    rst_n = 1;
    apb_wr(SPI_DIV, 1);
    apb_wr(SPI_CTRL, ON | C_CS_N);

    nreq = 0;
    while ($fscanf(f, " %c", k) == 1) begin
      case (k)
        "c": begin
          r = $fscanf(f, "%h", a0);
          spi_cmd(a0[7:0]);
        end
        "d": begin
          r = $fscanf(f, "%h", a0);
          apb_wr(SPI_DATA, a0);
        end
        "f": begin
          spi_wait;
          $fwrite(out, "f\n");
        end
        "m": begin
          r = $fscanf(f, "%h %h", a0, a1);
          dut.mem.mem0[a0[11:0]] = a1[7:0];
          dut.mem.mem1[a0[11:0]] = a1[15:8];
          dut.mem.mem2[a0[11:0]] = a1[23:16];
          dut.mem.mem3[a0[11:0]] = a1[31:24];
        end
        "w": begin
          r = $fscanf(f, "%h %h %h %h", a0, a1, a2, a3);
          spi_cmd(8'h2a); data16(a0); data16(a1);
          spi_cmd(8'h2b); data16(a2); data16(a3);
          spi_cmd(8'h2c);
        end
        "r": begin
          r = $fscanf(f, "%h %h %h %h %h %h %h", a0, a1, a2, a3, a4, a5, a6);
          spi_wait;
          apb_wr(SPI_CTRL, ON | C_DC | C_STREAM);
          apb_wr(12'h008, a1);              // FB_START
          apb_wr(12'h00C, a2);              // MAP
          apb_wr(12'h010, a3);              // CLUT
          apb_wr(12'h014, a4);              // LINE_OFFSET
          apb_wr(12'h018, a5);              // FIRST
          apb_wr(12'h01C, a6);              // SIZE
          apb_wr(12'h000, { 29'd0, a0[1:0], 1'b1 });   // FORMAT, START
          s = 0;
          while (!s[1])
            apb_rd(12'h004, s);
          apb_wr(12'h004, 32'h2);           // clear DONE
          spi_wait;
          apb_wr(SPI_CTRL, ON | C_DC);
          nreq = nreq + 1;
        end
        default: begin
          $display("error: steps.txt: unknown step %c", k);
          $finish;
        end
      endcase
    end
    spi_wait;
    apb_wr(SPI_CTRL, ON | C_CS_N);
    $fclose(out);
    $display("%0d requests", nreq);
    $finish;
  end

endmodule
