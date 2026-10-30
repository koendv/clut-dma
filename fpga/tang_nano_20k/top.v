// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// top: Tang Nano 20K. Gowin Cortex-M1 with clut_fabric (clut_dma, frame
// memory, spi display master) and a WeAct ST7789 240x280 display.
//   M1 APB extension master 1 at 0x60000000: clut_fabric registers
//   M1 AHB extension master 1 at 0x80000000: frame memory, 16 kbyte
//   clut_dma interrupt: EXTINT[0]
//   clock: 27 MHz crystal, rPLL to 54 MHz

module top (
  input  clk27,       // 27 MHz crystal
  input  btn_s1,      // reset, high when pressed
  input  btn_s2,      // user button, high when pressed
  output led_n,       // led, low is on
  output mark,        // timing mark, for a logic analyzer

  inout  swdio,       // to the debug probe
  inout  swclk,

  output uart_tx,     // to the BL616 usb serial
  input  uart_rx,

  output lcd_sck,
  output lcd_mosi,
  output lcd_dc,
  output lcd_cs_n,
  output lcd_rst_n,
  output lcd_bl
);

  // clock: 27 MHz * (FBDIV_SEL + 1) / (IDIV_SEL + 1) = 54 MHz, vco 54 * 16 = 864 MHz
  wire hclk, pll_lock;

  rPLL #(
    .FCLKIN("27"),
    .DYN_IDIV_SEL("false"),
    .IDIV_SEL(0),
    .DYN_FBDIV_SEL("false"),
    .FBDIV_SEL(1),
    .DYN_ODIV_SEL("false"),
    .ODIV_SEL(16),
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

  // APB extension master 1
  wire [31:0] paddr, pwdata, prdata;
  wire        psel, penable, pwrite, pready, pslverr;

  // AHB extension master 1
  wire [31:0] haddr, hwdata, hrdata;
  wire [1:0]  htrans;
  wire [2:0]  hsize;
  wire        hsel, hwrite, hready, hreadyout, hresp;

  wire        irq;
  wire [1:0]  gpio_out;

  Gowin_EMPU_M1_Top m1 (
    .LOCKUP(),
    .HALTED(),
    .EXTINT({ 3'b000, irq }),
    .JTAG_7(swdio),
    .JTAG_9(swclk),
    .UART0RXD(uart_rx),
    .UART0TXD(uart_tx),
    .APB1PADDR(paddr),
    .APB1PENABLE(penable),
    .APB1PWRITE(pwrite),
    .APB1PSTRB(),
    .APB1PPROT(),
    .APB1PWDATA(pwdata),
    .APB1PSEL(psel),
    .APB1PRDATA(prdata),
    .APB1PREADY(pready),
    .APB1PSLVERR(pslverr),
    .APB1PCLK(),
    .APB1PRESET(),
    .AHB1HRDATA(hrdata),
    .AHB1HREADYOUT(hreadyout),
    .AHB1HRESP({ 1'b0, hresp }),
    .AHB1HTRANS(htrans),
    .AHB1HBURST(),
    .AHB1HPROT(),
    .AHB1HSIZE(hsize),
    .AHB1HWRITE(hwrite),
    .AHB1HREADYMUX(hready),
    .AHB1HMASTER(),
    .AHB1HMASTLOCK(),
    .AHB1HADDR(haddr),
    .AHB1HWDATA(hwdata),
    .AHB1HSEL(hsel),
    .AHB1HCLK(),
    .AHB1HRESET(),
    .HCLK(hclk),
    .hwRstn(rst_n)
  );

  clut_fabric fabric (
    .clk(hclk), .rst_n(rst_n),
    .psel(psel), .penable(penable), .pwrite(pwrite), .paddr(paddr[11:0]),
    .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr),
    .hsel(hsel), .haddr(haddr), .htrans(htrans), .hwrite(hwrite),
    .hsize(hsize), .hwdata(hwdata), .hready(hready),
    .hreadyout(hreadyout), .hrdata(hrdata), .hresp(hresp),
    .irq(irq),
    .gpio_out(gpio_out), .gpio_in(btn_s2),
    .lcd_sck(lcd_sck), .lcd_mosi(lcd_mosi), .lcd_dc(lcd_dc),
    .lcd_cs_n(lcd_cs_n), .lcd_rst_n(lcd_rst_n), .lcd_bl(lcd_bl)
  );

  assign led_n = !gpio_out[0];
  assign mark  = gpio_out[1];

endmodule
