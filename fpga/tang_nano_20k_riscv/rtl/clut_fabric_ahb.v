// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_fabric_ahb: clut_dma and an spi display master, for a cpu with an
// AHB-Lite slave port, an AHB-Lite master port into its memory and an APB
// slave port (GoRV32).
//   AHB slave: clut_dma registers, through ahb_reg_slave. byte offsets
//     0x00..0x1F, repeating every 32 bytes.
//   AHB master: clut_dma reads frame buffer, tile map and clut from the cpu
//     memory, through ahb_rd_master. FB, MAP and CLUT are cpu addresses.
//   APB, byte offsets: 0x40..0x4F spi_lcd, 0x80 GPIO, as in clut_fabric.
//     GPIO: bits 1:0 outputs (read back), bit 8 input. other offsets read 0.
// All on one clock: the byte stream from clut_dma to spi_lcd is synchronous.

module clut_fabric_ahb (
  input             clk,
  input             rst_n,

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

  // AHB-Lite master
  output     [31:0] m_haddr,
  output     [1:0]  m_htrans,
  output            m_hwrite,
  output     [2:0]  m_hsize,
  output     [2:0]  m_hburst,
  output     [3:0]  m_hprot,
  output     [31:0] m_hwdata,
  input             m_hready,
  input      [31:0] m_hrdata,
  input             m_hresp,

  // APB3 slave
  input             psel,
  input             penable,
  input             pwrite,
  input      [11:0] paddr,
  input      [31:0] pwdata,
  output     [31:0] prdata,
  output            pready,
  output            pslverr,

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

  // the APB slot is decoded by the cpu: offsets 0x00..0xff
  wire unused = &{ 1'b0, paddr[11:8], paddr[5:4] };

  wire sel_spi  = paddr[7:6] == 2'd1;
  wire sel_gpio = paddr[7:6] == 2'd2;

  wire [31:0] spi_prdata;
  wire        spi_pready, spi_pslverr;

  wire        reg_wr, reg_rd;
  wire [4:2]  reg_addr;
  wire [31:0] reg_wdata, reg_rdata;

  wire        rd_req, rd_ack, rd_err;
  wire [31:0] rd_addr, rd_data;

  wire        byte_valid, byte_ready;
  wire [7:0]  byte_data;

  reg [1:0]   gpio_q;
  reg [1:0]   gpio_in_sync;
  reg [31:0]  gpio_prdata;

  assign gpio_out = gpio_q;

  assign prdata  = sel_spi ? spi_prdata  : sel_gpio ? gpio_prdata : 32'd0;
  assign pready  = sel_spi ? spi_pready  : 1'b1;
  assign pslverr = sel_spi ? spi_pslverr : 1'b0;

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

  ahb_reg_slave regs (
    .clk(clk), .rst_n(rst_n),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata)
  );

  clut_dma dma (
    .clk(clk), .rst_n(rst_n),
    .reg_wr(reg_wr), .reg_rd(reg_rd), .reg_addr(reg_addr),
    .reg_wdata(reg_wdata), .reg_rdata(reg_rdata),
    .irq(irq),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack),
    .rd_data(rd_data), .rd_err(rd_err),
    .byte_valid(byte_valid), .byte_data(byte_data), .byte_ready(byte_ready)
  );

  ahb_rd_master rdm (
    .clk(clk), .rst_n(rst_n),
    .rd_req(rd_req), .rd_addr(rd_addr), .rd_ack(rd_ack),
    .rd_data(rd_data), .rd_err(rd_err),
    .haddr(m_haddr), .htrans(m_htrans), .hwrite(m_hwrite), .hsize(m_hsize),
    .hburst(m_hburst), .hprot(m_hprot), .hwdata(m_hwdata),
    .hready(m_hready), .hrdata(m_hrdata), .hresp(m_hresp)
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
