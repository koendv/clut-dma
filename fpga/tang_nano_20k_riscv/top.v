// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// top: Tang Nano 20K. Gowin GoRV32 with clut_fabric_ahb (clut_dma, spi
// display master) and a WeAct ST7789 240x280 display. clut_dma reads the
// frame buffer, tile map and clut from the GoRV32 On-Chip RAM.
//   GoRV32 AHB slave port, 0xE8000000: clut_dma registers
//   GoRV32 AHB master port: clut_dma reads, On-Chip RAM at 0x40000000
//   GoRV32 APB slave port, 0xF8000000: spi_lcd (+0x40), gpio (+0x80)
//   clut_dma interrupt: EXT_INT[0]
//   clock: 27 MHz crystal, rPLL to 24 MHz, for the AHB and the APB
//   debug: GoRV32 JTAG on header J6

module top (
  input  clk27,       // 27 MHz crystal
  input  btn_s1,      // reset, high when pressed
  input  btn_s2,      // user button, high when pressed
  output led_n,       // led, low is on
  output mark,        // timing mark, for a logic analyzer

  input  jtag_tck,    // to the debug probe
  input  jtag_tms,
  input  jtag_tdi,
  output jtag_tdo,

  output uart_tx,     // to the BL616 usb serial
  input  uart_rx,

  output lcd_sck,
  output lcd_mosi,
  output lcd_dc,
  output lcd_cs_n,
  output lcd_rst_n,
  output lcd_bl
);

  // clock: 27 MHz * (FBDIV_SEL + 1) / (IDIV_SEL + 1) = 27 * 8 / 9 = 24 MHz,
  // pfd 27 / 9 = 3 MHz, vco 24 * 32 = 768 MHz.
  // GoRV32 does not close at 50 or 48 MHz on the GW2AR-18: Fmax about 35 MHz.
  wire hclk, pll_lock;

  rPLL #(
    .FCLKIN("27"),
    .DYN_IDIV_SEL("false"),
    .IDIV_SEL(8),
    .DYN_FBDIV_SEL("false"),
    .FBDIV_SEL(7),
    .DYN_ODIV_SEL("false"),
    .ODIV_SEL(32),
    .PSDA_SEL("0000"),
    .DYN_DA_EN("true"),
    .DUTYDA_SEL("1000"),
    .CLKOUT_FT_DIR(1'b1),
    .CLKOUTP_FT_DIR(1'b1),
    .CLKOUT_DLY_STEP(0),
    .CLKOUTP_DLY_STEP(0),
    .CLKFB_SEL("internal"),
    .CLKOUT_BYPASS("false"),
    .CLKOUTP_BYPASS("false"),
    .CLKOUTD_BYPASS("false"),
    .DYN_SDIV_SEL(2),
    .CLKOUTD_SRC("CLKOUT"),
    .CLKOUTD3_SRC("CLKOUT"),
    .DEVICE("GW2AR-18C")
  ) pll (
    .CLKOUT(hclk),
    .LOCK(pll_lock),
    .CLKOUTP(),
    .CLKOUTD(),
    .CLKOUTD3(),
    .RESET(1'b0),
    .RESET_P(1'b0),
    .CLKIN(clk27),
    .CLKFB(1'b0),
    .FBDSEL(6'd0),
    .IDSEL(6'd0),
    .ODSEL(6'd0),
    .PSDA(4'd0),
    .DUTYDA(4'd0),
    .FDLY(4'd0)
  );

  // reset: asserted asynchronously, released synchronously
  wire       rst_in_n = pll_lock && !btn_s1;
  reg  [1:0] rst_sync;
  always @(posedge hclk or negedge rst_in_n)
    if (!rst_in_n)
      rst_sync <= 2'b00;
    else
      rst_sync <= { rst_sync[0], 1'b1 };
  wire rst_n = rst_sync[1];

  // AHB slave port: EXTS_HREADYOUT is HREADY to the slave, EXTS_HREADYIN the
  // slave's HREADYOUT (as in the ahb_slave reference design of Gowin)
  wire [31:0] s_haddr, s_hwdata, s_hrdata;
  wire [1:0]  s_htrans;
  wire [2:0]  s_hsize;
  wire        s_hsel, s_hwrite, s_hready, s_hreadyout, s_hresp;

  // AHB master port
  wire [31:0] m_haddr, m_hwdata, m_hrdata;
  wire [1:0]  m_htrans, m_hresp;
  wire [2:0]  m_hsize, m_hburst;
  wire [3:0]  m_hprot;
  wire        m_hwrite, m_hready;

  // APB slave port
  wire [31:0] paddr, pwdata, prdata;
  wire        psel, penable, pwrite, pready, pslverr;

  wire        irq;
  wire [1:0]  gpio_out;

  Gowin_GoRV32_Top cpu (
    .HCLK(hclk),
    .PCLK(hclk),
    .POR_N(rst_n),
    // UART1
    .UART1_CTSN(1'b0),
    .UART1_DCDN(1'b0),
    .UART1_DSRN(1'b0),
    .UART1_RIN(1'b0),
    .UART1_RXD(uart_rx),
    .UART1_DTRN(),
    .UART1_OUT1N(),
    .UART1_OUT2N(),
    .UART1_RTSN(),
    .UART1_TXD(uart_tx),
    // QSPI flash: not used, the firmware runs from On-Chip RAM
    .FLASH_QSPI_CSN(),
    .FLASH_QSPI_MISO(),
    .FLASH_QSPI_MOSI(),
    .FLASH_QSPI_CLK(),
    .FLASH_QSPI_HOLDN(),
    .FLASH_QSPI_WPN(),
    // user interrupts
    .EXT_INT({ 7'd0, irq }),
    // AHB slave port
    .EXTS_HRDATA(s_hrdata),
    .EXTS_HREADYIN(s_hreadyout),
    .EXTS_HRESP({ 1'b0, s_hresp }),
    .EXTS_HREADYOUT(s_hready),
    .EXTS_HADDR(s_haddr),
    .EXTS_HBURST(),
    .EXTS_HPROT(),
    .EXTS_HSEL(s_hsel),
    .EXTS_HSIZE(s_hsize),
    .EXTS_HTRANS(s_htrans),
    .EXTS_HWDATA(s_hwdata),
    .EXTS_HWRITE(s_hwrite),
    .EXTS_HCLK(),
    .EXTS_HRSTN(),
    // AHB master port
    .EXTM_HADDR(m_haddr),
    .EXTM_HBURST(m_hburst),
    .EXTM_HPROT(m_hprot),
    .EXTM_HRDATA(m_hrdata),
    .EXTM_HREADY(m_hready),
    .EXTM_HRESP(m_hresp),
    .EXTM_HSIZE(m_hsize),
    .EXTM_HTRANS(m_htrans),
    .EXTM_HWRITE(m_hwrite),
    .EXTM_HWDATA(m_hwdata),
    // APB slave port
    .APB_PPROT(),
    .APB_PSTRB(),
    .APB_PADDR(paddr),
    .APB_PENABLE(penable),
    .APB_PWRITE(pwrite),
    .APB_PWDATA(pwdata),
    .APB_PSEL(psel),
    .APB_PRDATA(prdata),
    .APB_PREADY(pready),
    .APB_PSLVERR(pslverr),
    .APB_PCLK(),
    .APB_PRSTN(),
    // debug
    .JTAG_TMS(jtag_tms),
    .JTAG_TDI(jtag_tdi),
    .JTAG_TDO(jtag_tdo),
    .JTAG_TCK(jtag_tck)
  );

  clut_fabric_ahb fabric (
    .clk(hclk), .rst_n(rst_n),
    .hsel(s_hsel), .haddr(s_haddr), .htrans(s_htrans), .hwrite(s_hwrite),
    .hsize(s_hsize), .hwdata(s_hwdata), .hready(s_hready),
    .hreadyout(s_hreadyout), .hrdata(s_hrdata), .hresp(s_hresp),
    .m_haddr(m_haddr), .m_htrans(m_htrans), .m_hwrite(m_hwrite),
    .m_hsize(m_hsize), .m_hburst(m_hburst), .m_hprot(m_hprot),
    .m_hwdata(m_hwdata), .m_hready(m_hready), .m_hrdata(m_hrdata),
    .m_hresp(m_hresp != 2'b00),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr[11:0]),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .irq(irq),
    .gpio_out(gpio_out), .gpio_in(btn_s2),
    .lcd_sck(lcd_sck), .lcd_mosi(lcd_mosi), .lcd_dc(lcd_dc),
    .lcd_cs_n(lcd_cs_n), .lcd_rst_n(lcd_rst_n), .lcd_bl(lcd_bl)
  );

  assign led_n = !gpio_out[0];
  assign mark  = gpio_out[1];

endmodule
