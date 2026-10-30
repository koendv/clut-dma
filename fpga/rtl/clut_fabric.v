// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_fabric: clut_dma, its memory and an spi display master, for a cpu with
// one APB slot and one AHB slot.
//   APB, byte offsets: 0x00..0x1F clut_dma, 0x40..0x4F spi_lcd, 0x80 GPIO
//     GPIO: bits 1:0 outputs (read back), bit 8 input
//   AHB: fbmem, frame buffer, tile map and clut. FB, MAP and CLUT of clut_dma
//        are cpu addresses in this slot.

module clut_fabric (
  input             clk,
  input             rst_n,

  // APB3 slave
  input             psel,
  input             penable,
  input             pwrite,
  input      [11:0] paddr,
  input      [31:0] pwdata,
  output     [31:0] prdata,
  output            pready,
  output            pslverr,

  // AHB-Lite slave
  input             hsel,
  input      [31:0] haddr,
  input      [1:0]  htrans,
  input             hwrite,
  input      [2:0]  hsize,
  input      [31:0] hwdata,
  input             hready,
  output            hreadyout,
  output     [31:0] hrdata,
  output            hresp,

  output            irq,

  // gpio
  output     [1:0]  gpio_out,
  input             gpio_in,

  // display
  output            lcd_sck,
  output            lcd_mosi,
  output            lcd_dc,
  output            lcd_cs_n,
  output            lcd_rst_n,
  output            lcd_bl
);

  // the slot is decoded by the cpu: offsets 0x00..0xff
  wire unused = &{ 1'b0, paddr[11:8], paddr[5] };

  wire sel_spi  = paddr[7:6] == 2'd1;
  wire sel_gpio = paddr[7:6] == 2'd2;
  wire sel_dma  = paddr[7:6] == 2'd0;

  wire [31:0] dma_prdata, spi_prdata;
  wire        dma_pready, spi_pready, dma_pslverr, spi_pslverr;

  wire        rd_req, rd_ack;
  wire [31:0] rd_addr, rd_data;

  wire        byte_valid, byte_ready;
  wire [7:0]  byte_data;

  reg [1:0]   gpio_q;
  reg [1:0]   gpio_in_sync;
  reg [31:0]  gpio_prdata;
  assign gpio_out = gpio_q;

  assign prdata  = sel_spi ? spi_prdata  : sel_gpio ? gpio_prdata : dma_prdata;
  assign pready  = sel_spi ? spi_pready  : sel_gpio ? 1'b1 : dma_pready;
  assign pslverr = sel_spi ? spi_pslverr : sel_gpio ? 1'b0 : dma_pslverr;

  // gpio
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      gpio_q       <= 0;
      gpio_in_sync <= 0;
      gpio_prdata  <= 0;
    end else begin
      gpio_in_sync <= { gpio_in_sync[0], gpio_in };
      if (psel && sel_gpio && penable && pwrite)
        gpio_q <= pwdata[1:0];
      if (psel && sel_gpio && !penable && !pwrite)
        gpio_prdata <= { 23'd0, gpio_in_sync[1], 6'd0, gpio_q };
    end
  end

  clut_dma dma (
    .clk(clk), .rst_n(rst_n),
    .psel(psel && sel_dma), .penable(penable), .pwrite(pwrite),
    .paddr(paddr[4:0]), .pwdata(pwdata), .prdata(dma_prdata),
    .pready(dma_pready), .pslverr(dma_pslverr), .irq(irq),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready)
  );

  fbmem mem (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack), .rd_data(rd_data)
  );

  spi_lcd spi (
    .clk(clk), .rst_n(rst_n),
    .psel(psel && sel_spi), .penable(penable), .pwrite(pwrite),
    .paddr(paddr[3:0]), .pwdata(pwdata), .prdata(spi_prdata),
    .pready(spi_pready), .pslverr(spi_pslverr),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready),
    .sck(lcd_sck), .mosi(lcd_mosi), .dc(lcd_dc), .cs_n(lcd_cs_n),
    .lcd_rst_n(lcd_rst_n), .bl(lcd_bl)
  );

endmodule
