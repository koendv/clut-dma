// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// spi_lcd: spi master for a display with a d/c pin. spi mode 0, msb first.
// Bytes come from the cpu (DATA register) or from clut_dma (byte stream),
// selected by CTRL.STREAM. One tx data register and a shift register:
// back to back bytes take 16 * DIV clock cycles.
//
// Registers, APB3, byte offsets:
//   0x00 DATA    write: byte to send. PREADY is low while the data register is full.
//                in stream mode the write is an error: PSLVERR, the byte is dropped
//   0x04 CTRL    bit 0 DC, bit 1 CS_N, bit 2 RST_N, bit 3 BL,
//                bit 4 STREAM: bytes from clut_dma instead of DATA,
//                bits 15:8 BRIGHT: backlight pwm, on BRIGHT/256 of the time;
//                0 is always on. pwm at clk / 2^18, 206 Hz at 54 MHz
//   0x08 STATUS  bit 0 BUSY: a byte in the data register or in the shift register
//   0x0C DIV     sck half period in clock cycles, 1..255. sck = clk / (2 * DIV)
// Change DC and CS_N only when BUSY is 0.

module spi_lcd (
  input             clk,
  input             rst_n,

  // register port, APB3
  input             psel,
  input             penable,
  input             pwrite,
  input      [3:0]  paddr,
  input      [31:0] pwdata,
  output reg [31:0] prdata,
  output            pready,
  output            pslverr,

  // byte stream from clut_dma
  input             byte_valid,
  input      [7:0]  byte_data,
  output            byte_ready,

  // display
  output reg        sck,
  output            mosi,
  output            dc,
  output            cs_n,
  output            lcd_rst_n,
  output reg        bl
`ifdef FORMAL
  ,
  input      [31:0] f_wait        // wait cycles of the access phase, from fapb3_slave
`endif
);

  reg [4:0] ctrl;
  reg [7:0] bright;
  reg [17:0] pwm;
  reg [7:0] div;

  // tx data register
  reg [7:0] dr;
  reg       dr_full;

  // shift register
  reg [7:0] sh;
  reg [2:0] bitn;
  reg       shifting;
  reg [7:0] cnt;

  wire stream   = ctrl[4];
  wire busy     = dr_full || shifting;

  assign dc        = ctrl[0];
  assign cs_n      = ctrl[1];
  assign lcd_rst_n = ctrl[2];
  assign mosi      = sh[7];

  assign byte_ready = stream && !dr_full;

  // cpu byte: waits while the data register is full. in stream mode the data
  // register belongs to clut_dma: the write ends at once with PSLVERR, instead
  // of waiting for a gap in the stream that may never come
  wire data_write = psel && penable && pwrite && paddr[3:2] == 2'd0;
  wire data_err   = data_write && stream;
  wire cpu_push   = data_write && !stream && !dr_full;
  wire dma_push   = byte_valid && byte_ready;

  assign pready  = !data_write || data_err || cpu_push;
  assign pslverr = data_err;

  // registers are words, of up to 8 bits
  wire unused = &{ 1'b0, paddr[1:0], pwdata[31:16] };

  // registers, APB3
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      ctrl   <= 5'b00010;  // CS_N high, RST_N low
      bright <= 0;
      div    <= 8'd4;
      prdata <= 0;
    end else begin
      if (psel && penable && pwrite) begin
        case (paddr[3:2])
          2'd1: begin
                  ctrl   <= pwdata[4:0];
                  bright <= pwdata[15:8];
                end
          2'd3: div  <= (pwdata[7:0] == 0) ? 8'd1 : pwdata[7:0];
          default: ;
        endcase
      end
      if (psel && !penable && !pwrite) begin
        case (paddr[3:2])
          2'd1:    prdata <= { 16'd0, bright, 3'd0, ctrl };
          2'd2:    prdata <= { 31'd0, busy };
          2'd3:    prdata <= { 24'd0, div };
          default: prdata <= 0;
        endcase
      end
    end
  end

  // backlight
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      pwm <= 0;
      bl  <= 0;
    end else begin
      pwm <= pwm + 18'd1;
      bl  <= ctrl[3] && (bright == 0 || pwm[17:10] < bright);
    end
  end

  // data register and shift register
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      dr <= 0; dr_full <= 0;
      sh <= 0; bitn <= 0; shifting <= 0; cnt <= 0;
      sck <= 0;
    end else begin
      if (dma_push) begin
        dr      <= byte_data;
        dr_full <= 1;
      end else if (cpu_push) begin
        dr      <= pwdata[7:0];
        dr_full <= 1;
      end

      if (!shifting) begin
        if (dr_full) begin
          sh       <= dr;
          dr_full  <= 0;
          bitn     <= 0;
          cnt      <= div - 8'd1;
          shifting <= 1;
        end
      end else if (cnt != 0)
        cnt <= cnt - 8'd1;
      else begin
        cnt <= div - 8'd1;
        if (!sck)
          sck <= 1;           // rising edge: display samples mosi
        else begin
          sck <= 0;           // falling edge: next bit
          if (bitn != 7) begin
            sh   <= sh << 1;
            bitn <= bitn + 3'd1;
          end else if (dr_full) begin
            sh      <= dr;    // next byte, no gap
            dr_full <= 0;
            bitn    <= 0;
          end else
            shifting <= 0;
        end
      end
    end
  end

`ifdef FORMAL
  // Formal: helper invariants for the wait bound of formal/fapb3_slave.v
  // (formal/apb.sby, with the APB assumptions of fapb3_slave). A write to
  // DATA waits while the data register is full, until the shift register
  // takes the byte, at the end of the byte it is sending. f_wait is the wait
  // counter of fapb3_slave. The bound ties it to the shift state, so that
  // the proof need not count up to 16 * 255 cycles. A half period of
  // sck lasts at most 255 cycles (div is 8 bits, not 0), and the wait
  // started at least 2 cycles after the shift register took the byte it is
  // sending: then f_wait <= (2 * bitn + sck) * 255 + 253 - cnt.
  always @(*)
    if (rst_n) begin
      assert(div != 0);
      assert(cnt != 8'd255);
      if (!shifting)
        assert(!sck);
      if (f_wait != 0 && dr_full) begin
        assert(shifting);
        assert(f_wait + cnt <= (2 * bitn + sck) * 255 + 253);
      end
    end
`endif

endmodule
