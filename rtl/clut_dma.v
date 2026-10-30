// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_dma: color expansion of a 1bpp frame buffer with a per-tile color
// lookup table, in a DMA engine. Mock-up. Same output as
// u8x8_clut_stream_rgb565() and u8x8_clut_stream_rgb444() in u8g2
// (csrc/u8x8_clut.c), for a frame buffer in horizontal layout.
//
// Memory, little-endian:
//   bitmap  1 bit per pixel, horizontal: a byte is 8 pixels of a pixel row,
//           the leftmost pixel in bit 7. TILE_COLS bytes per pixel row.
//           as u8g2_ll_hvline_horizontal_right_lsb in u8g2. no alignment.
//   MAP     tile clut map: 4-bit clut index per 8x8 tile, tile t is nibble t,
//           even tile in the low nibble. word aligned.
//   CLUT    16 entries of 32 bits: foreground in bits 15:0, background in
//           bits 31:16. rgb565, or rgb444 in bits 11:0. 64-byte aligned.
//
// Request: a rectangle of W by H pixels, top left on a tile boundary at tile
// (tx, ty). TILE_COLS is the number of tiles per tile row, and the number of
// bitmap bytes per pixel row. The cpu writes
//   FB_START     bitmap address of the top left pixel: bitmap + 8*ty*TILE_COLS + tx
//   FIRST        top left tile: ty*TILE_COLS + tx
//   LINE_OFFSET  bitmap bytes of a pixel row outside W: TILE_COLS - W/8 rounded up.
//                in bytes, not in pixels as the line offset of other 2D DMA engines
//   SIZE         W and H in pixels
// The rectangle is sent pixel row by pixel row; the window of the display is
// set to the rectangle beforehand.
// FB_START and the tile number step by 1 per tile. After the last tile of a
// pixel row, FB_START steps by LINE_OFFSET + 1 to the next pixel row; after
// the last tile of a tile row, the tile number steps by LINE_OFFSET + 1 to the
// next tile row.
// As in a DMA channel, the engine updates FB_START, FIRST and H during the
// transfer; write them again for the next request. W need not be a multiple
// of 8; for rgb444 W must be even.
//
// Output: a stream of bytes, valid/ready, for a byte-wide peripheral (SPI).
// Bytes as u8x8_clut_stream_rgb565/444() send them:
//   rgb565: 2 bytes per pixel, most significant byte first.
//   rgb444: 3 bytes per two pixels, {pixel0, pixel1} most significant byte first.
//   rgb666: 3 bytes per pixel, r, g, b, 6 bits each in bits 7:2.
//           from the rgb565 clut, 5-bit red and blue extended by replication.
// Two stages, working in parallel:
//   fetch:  addresses and memory reads. per tile and pixel row the clut index,
//           foreground and background, and the pixels of the tile in that row,
//           into the handoff register. starts on the next tile as soon as the
//           output has taken the handoff register.
//   output: from the handoff register, the bytes of up to 8 pixels.
// so the memory reads of a tile overlap the output of the previous tile.
// The fetch keeps the last map word, the last bitmap word, and the clut index
// of the colors in the handoff register. It reads a map word at a multiple
// of 8 tiles, a bitmap word at a word boundary, both also at the first tile
// of a pixel row, and a clut word
// when the index changes. A change in memory during a transfer may take effect
// only at the next read of that word.
// Parameter CACHE = 0: no kept words. Every tile reads the map word, the clut
// word and the bitmap word, and keeps only the nibble and the byte it needs.
// The same output, more reads; to tell a fault in the cache logic from one
// elsewhere.
// A 2-byte skid buffer between output and peripheral.
// DONE is set when the last byte has left the buffer.
//
// Registers, APB3, byte offsets:
//   0x00 CTRL         bit 0 START (write 1),
//                     bits 2:1 FORMAT (0 rgb565, 1 rgb444, 2 rgb666),
//                     bit 3 IRQ_EN
//   0x04 STATUS       bit 0 BUSY, bit 1 DONE (write 1 to clear)
//   0x08 FB_START
//   0x0C MAP          word aligned: bits 1:0 are ignored and read as 0
//   0x10 CLUT         64-byte aligned: bits 5:0 are ignored and read as 0
//   0x14 LINE_OFFSET  bitmap bytes of a pixel row outside W, 0..4094
//   0x18 FIRST        top left tile of the rectangle
//   0x1C SIZE         bits 11:0 W, bits 27:16 H, in pixels
//
// All outputs come from flip-flops.

module clut_dma #(
  parameter CACHE = 1             // keep the last map, bitmap and clut word
) (
  input             clk,
  input             rst_n,

  // register port, APB3
  input             psel,
  input             penable,
  input             pwrite,
  input      [4:0]  paddr,
  input      [31:0] pwdata,
  output reg [31:0] prdata,
  output            pready,
  output            pslverr,
  output reg        irq,

  // memory read port: rd_addr is stable while rd_req is high,
  // rd_data is valid when rd_ack is high
  output reg        rd_req,
  output reg [31:0] rd_addr,
  input             rd_ack,
  input      [31:0] rd_data,

  // byte stream
  output reg        byte_valid,
  output reg [7:0]  byte_data,
  input             byte_ready
);

  // fetch states
  localparam S_IDLE  = 4'd0;
  localparam S_WAIT  = 4'd2;
  localparam S_MAP   = 4'd3;
  localparam S_CLUT  = 4'd4;
  localparam S_FB    = 4'd5;
  localparam S_IDX   = 4'd6;
  localparam S_NEXT  = 4'd7;
  localparam S_DRAIN = 4'd8;
  localparam S_PUT   = 4'd9;

  localparam F_RGB565 = 2'd0;
  localparam F_RGB444 = 2'd1;
  localparam F_RGB666 = 2'd2;

  // registers
  reg [31:0] fb_start;
  reg [31:2] map;
  reg [31:6] clut;
  reg [11:0] line_offset, first, w, h;
  reg [1:0]  fmt;
  reg        irq_en, busy, done;

  // fetch
  reg [3:0]  state;
  // FB_START: bitmap address of the current tile. FIRST: first tile of the
  // tile row. H: pixel rows left.
  reg [11:0] tile;    // current tile, for the map
  reg [11:0] c;       // tile in the pixel row, from tile column tx on
  reg [2:0]  row;     // pixel row in the tile row
  reg [31:0] map_word; // last map word
  reg [31:0] fb_word;  // last bitmap word
  reg [3:0]  idx;      // clut index of the colors in h_fg, h_bg
  reg        clut_valid;

  // handoff register, from fetch to output
  reg [8:0]  h_sh;    // pixels, leftmost in bit 0, sentinel above the last pixel
  reg [15:0] h_fg, h_bg;
  reg        h_valid;

  // output
  reg [15:0] fg, bg;
  reg [8:0]  sh;      // pixels, leftmost in bit 0, sentinel above. empty: 9'd1
  reg [2:0]  bsel;    // byte of the pixel (pair), one-hot

  // skid buffer: byte_data/byte_valid is the head, skid_data/skid_valid the second byte
  reg [7:0]  skid_data;
  reg        skid_valid;

  assign pready  = 1'b1;
  assign pslverr = 1'b0;

  // registers are words: the byte offset is not used
  wire unused = &{ 1'b0, paddr[1:0] };

  wire apb_write = psel & penable & pwrite;
  wire apb_read  = psel & ~penable & ~pwrite;   // setup phase: prdata valid in access phase
  wire start     = apb_write && paddr[4:2] == 3'd0 && pwdata[0];

  // map word of a tile: 8 tiles per word
  function [31:0] map_addr(input [8:0] t_word);
    map_addr = { map + { 21'd0, t_word }, 2'b00 };
  endfunction

  // tiles per pixel row, and pixels in the last tile of the row
  wire [11:0] row_tiles = { 3'd0, w[11:3] } + { 11'd0, w[2:0] != 3'd0 };
  wire [3:0]  last_px   = (w[2:0] == 3'd0) ? 4'd8 : { 1'b0, w[2:0] };

  // clut index of the tile, from the map word
  wire [3:0]  tile_idx  = map_word[{ tile[2:0], 2'b00 } +: 4];

  // a step to the next tile, after a read
  wire [11:0] c_next   = c + 12'd1;
  wire        step     = state == S_NEXT;
  // after the last tile of the last pixel row: done
  wire        req_end  = state == S_NEXT && h == 12'd1 && c_next == row_tiles;
  wire        row_end  = step && c_next == row_tiles && !req_end;
  // at the end of a pixel row, FB_START over the tiles outside W; at the end
  // of a tile row, the tile number too
  wire        grp_end  = row_end && row == 3'd7;
  wire [11:0] tile_next = tile + (grp_end ? line_offset : 12'd0) + 12'd1;
  wire [31:0] fbp      = fb_start;

  // reads for this tile: at the first tile of a pixel row, at a word
  // boundary, when the clut index changes. without cache: always
  wire        need_map = !CACHE || c == 12'd0 || tile[2:0] == 3'd0;
  wire        need_fb  = !CACHE || c == 12'd0 || fbp[1:0] == 2'd0;
  wire        need_clut = !CACHE || !clut_valid || tile_idx != idx;

  // without cache: the clut index and the bitmap byte straight from the read
  wire [3:0]  rd_idx   = rd_data[{ tile[2:0], 2'b00 } +: 4];
  wire [7:0]  rd_byte  = rd_data[{ fbp[1:0], 3'b000 } +: 8];

  // bitmap byte of the tile, from the bitmap word. leftmost pixel in bit 7,
  // reversed: leftmost in bit 0
  wire [7:0]  fb_byte  = CACHE ? fb_word[{ fbp[1:0], 3'b000 } +: 8] : fb_word[7:0];
  wire [7:0]  px       = { fb_byte[0], fb_byte[1], fb_byte[2], fb_byte[3],
                           fb_byte[4], fb_byte[5], fb_byte[6], fb_byte[7] };

  // pixels of the tile and the sentinel above the last one
  wire [3:0]  npx     = (c_next == row_tiles) ? last_px : 4'd8;
  wire [8:0]  px_mask = (9'd1 << npx) - 9'd1;
  wire [8:0]  px_sh   = ({ 1'b0, px } & px_mask) | (9'd1 << npx);

  // fg/bg select
  wire [15:0] pix0 = sh[0] ? fg : bg;
  wire [11:0] pix1 = sh[1] ? fg[11:0] : bg[11:0];   // rgb444 only

  // shift register after this pixel (pair)
  wire [8:0] sh_next = (fmt == F_RGB444) ? (sh >> 2) : (sh >> 1);

  // bytes of the pixel (pair)
  reg [23:0] bytes;
  always @(*) begin
    case (fmt)
      F_RGB444: bytes = { pix0[11:0], pix1[11:0] };
      F_RGB666: bytes = { pix0[15:11], pix0[15], 2'b00,
                          pix0[10:5], 2'b00,
                          pix0[4:0], pix0[4], 2'b00 };
      default:  bytes = { pix0, 8'h00 };
    endcase
  end
  wire       last    = (fmt == F_RGB565) ? bsel[1] : bsel[2];
  wire [7:0] out_byte = bsel[0] ? bytes[23:16] : bsel[1] ? bytes[15:8] : bytes[7:0];

  // output: a byte into the skid buffer when there is room. empty: only the
  // sentinel left (or nothing, after an odd number of pixels in rgb444)
  wire sh_empty      = sh[8:1] == 8'd0;
  wire sh_next_empty = sh_next[8:1] == 8'd0;
  wire push          = !sh_empty && !skid_valid;
  wire pop           = byte_valid && byte_ready;

  // output takes the handoff register when empty, or with the last byte of a tile
  wire take = h_valid && (sh_empty || (push && last && sh_next_empty));

  // fetch puts the handoff register with the bitmap byte
  wire put  = state == S_PUT;

  // skid buffer
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      byte_valid <= 0; byte_data <= 0;
      skid_valid <= 0; skid_data <= 0;
    end else if (!byte_valid || pop) begin
      // head is free: take the second byte, or the new byte
      if (skid_valid) begin
        byte_data  <= skid_data;
        byte_valid <= 1;
        skid_valid <= 0;
      end else begin
        byte_data  <= out_byte;
        byte_valid <= push;
      end
    end else if (push) begin
      // head is waiting: new byte into the second place
      skid_data  <= out_byte;
      skid_valid <= 1;
    end
  end

  // registers, APB3
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      fb_start <= 0; map <= 0; clut <= 0;
      line_offset <= 0; first <= 0; w <= 0; h <= 0;
      fmt <= 0; irq_en <= 0;
      prdata <= 0;
      irq <= 0;
    end else begin
      // writes ignored while busy, except STATUS
      if (apb_write) begin
        case (paddr[4:2])
          3'd0: if (!busy) begin
                  fmt    <= pwdata[2:1];
                  irq_en <= pwdata[3];
                end
          3'd2: if (!busy) fb_start <= pwdata;
          3'd3: if (!busy) map <= pwdata[31:2];
          3'd4: if (!busy) clut <= pwdata[31:6];
          3'd5: if (!busy) line_offset <= pwdata[11:0];
          3'd6: if (!busy) first <= pwdata[11:0];
          3'd7: if (!busy) begin
                  w <= pwdata[11:0];
                  h <= pwdata[27:16];
                end
          default: ;
        endcase
      end
      if (step)
        fb_start <= fb_start + { 20'd0, row_end ? line_offset : 12'd0 } + 32'd1;
      if (row_end || req_end)
        h <= h - 12'd1;
      if (grp_end)
        first <= tile_next;
      if (apb_read) begin
        case (paddr[4:2])
          3'd0:    prdata <= { 28'd0, irq_en, fmt, 1'b0 };
          3'd1:    prdata <= { 30'd0, done, busy };
          3'd2:    prdata <= fb_start;
          3'd3:    prdata <= { map, 2'b00 };
          3'd4:    prdata <= { clut, 6'd0 };
          3'd5:    prdata <= { 20'd0, line_offset };
          3'd6:    prdata <= { 20'd0, first };
          default: prdata <= { 4'd0, h, 4'd0, w };
        endcase
      end
      irq <= done & irq_en;
    end
  end

  // handoff register valid
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      h_valid <= 0;
    else if (put)
      h_valid <= 1;
    else if (take)
      h_valid <= 0;
  end

  // output: the pixels from the handoff register, a byte per cycle while the
  // skid buffer has room. after the last byte of a pixel (pair): shift.
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      fg <= 0; bg <= 0; sh <= 9'd1; bsel <= 3'b001;
    end else if (take) begin
      fg   <= h_fg;
      bg   <= h_bg;
      sh   <= h_sh;
      bsel <= 3'b001;
    end else if (push) begin
      if (!last)
        bsel <= bsel << 1;
      else begin
        bsel <= 3'b001;
        sh   <= sh_next;
      end
    end
  end

  // fetch
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      busy <= 0; done <= 0;
      state <= S_IDLE;
      rd_req <= 0; rd_addr <= 0;
      tile <= 0; c <= 0; row <= 0;
      map_word <= 0; fb_word <= 0; idx <= 0; clut_valid <= 0;
      h_sh <= 0; h_fg <= 0; h_bg <= 0;
    end else begin

      if (apb_write && paddr[4:2] == 3'd1 && pwdata[1])
        done <= 0;

      case (state)
        S_IDLE:
          if (start) begin
            tile  <= first;
            c     <= 0;
            row   <= 0;
            clut_valid <= 0;
            busy  <= 1;
            done  <= 0;
            state <= (w == 0 || h == 0) ? S_DRAIN : S_WAIT;
          end

        // until the output has taken the handoff register. then the map
        // word if needed, else the clut index from the held map word
        S_WAIT:
          if (!h_valid) begin
            if (need_map) begin
              rd_req  <= 1;
              rd_addr <= map_addr(tile[11:3]);
              state   <= S_MAP;
            end else if (need_clut) begin
              idx     <= tile_idx;
              rd_req  <= 1;
              rd_addr <= { clut, tile_idx, 2'b00 };
              state   <= S_CLUT;
            end else if (need_fb) begin
              rd_req  <= 1;
              rd_addr <= { fbp[31:2], 2'b00 };
              state   <= S_FB;
            end else
              state <= S_PUT;
          end

        S_MAP:
          if (rd_ack) begin
            if (CACHE) begin
              map_word <= rd_data;
              rd_req   <= 0;
              state    <= S_IDX;
            end else begin
              rd_addr  <= { clut, rd_idx, 2'b00 };
              state    <= S_CLUT;
            end
          end

        // the clut index from the map word just read
        S_IDX:
          if (need_clut) begin
            idx     <= tile_idx;
            rd_req  <= 1;
            rd_addr <= { clut, tile_idx, 2'b00 };
            state   <= S_CLUT;
          end else if (need_fb) begin
            rd_req  <= 1;
            rd_addr <= { fbp[31:2], 2'b00 };
            state   <= S_FB;
          end else
            state <= S_PUT;

        S_CLUT:
          if (rd_ack) begin
            h_fg       <= rd_data[15:0];
            h_bg       <= rd_data[31:16];
            clut_valid <= 1;
            if (need_fb) begin
              rd_addr <= { fbp[31:2], 2'b00 };
              state   <= S_FB;
            end else begin
              rd_req  <= 0;
              state   <= S_PUT;
            end
          end

        S_FB:
          if (rd_ack) begin
            fb_word <= CACHE ? rd_data : { 24'd0, rd_byte };
            rd_req  <= 0;
            state   <= S_PUT;
          end

        // the bitmap byte from the bitmap word, into the handoff register
        S_PUT:
          begin
            h_sh  <= px_sh;
            state <= S_NEXT;
          end

        // FB_START, H and FIRST step in the register block
        S_NEXT:
          if (req_end)
            state <= S_DRAIN;
          else if (row_end) begin
            c     <= 0;
            row   <= row + 3'd1;
            tile  <= (row == 3'd7) ? tile_next : first;
            state <= S_WAIT;
          end else begin
            c     <= c_next;
            tile  <= tile_next;
            state <= S_WAIT;
          end

        // done when the output is empty and the last byte has left the skid buffer
        S_DRAIN:
          if (!h_valid && sh_empty && !byte_valid && !skid_valid) begin
            busy  <= 0;
            done  <= 1;
            state <= S_IDLE;
          end

        default:
          state <= S_IDLE;
      endcase
    end
  end

`ifdef FORMAL
  // Formal properties, for SymbiYosys (formal/clut_dma.sby). Assumptions
  // about the inputs, assertions about the outputs and the internal state.

  reg f_past_valid = 0;
  always @(posedge clk)
    f_past_valid <= 1;

  // start in reset
  always @(*)
    if (!f_past_valid)
      assume(!rst_n);

  // out of reset now and in the previous cycle
  reg  f_rst_n_d = 0;
  always @(posedge clk)
    f_rst_n_d <= rst_n;
  wire f_run = f_past_valid && rst_n && f_rst_n_d;

  // memory: rd_ack only during a request, not in two cycles in a row
  always @(*)
    if (rd_ack)
      assume(rd_req);
  always @(posedge clk)
    if (f_past_valid && $past(rd_ack))
      assume(!rd_ack);

  // a request in rgb444 has an even width
  always @(*)
    if (start && pwdata[2:1] == F_RGB444)
      assume(!w[0]);

  // memory read port: address stable and word aligned during a request
  always @(posedge clk)
    if (f_run && $past(rd_req && !rd_ack))
      assert(rd_req && $stable(rd_addr));
  always @(*)
    if (rd_req)
      assert(rd_addr[1:0] == 2'b00);

  // byte stream: a byte stays until it is taken
  always @(posedge clk)
    if (f_run && $past(byte_valid && !byte_ready))
      assert(byte_valid && $stable(byte_data));

  // interrupt
  always @(posedge clk)
    if (f_run)
      assert(irq == $past(done && irq_en));

  // fetch: legal states, busy outside S_IDLE, a read in the read states
  wire f_fetch = state == S_WAIT || state == S_MAP || state == S_CLUT ||
                 state == S_FB || state == S_IDX || state == S_PUT ||
                 state == S_NEXT;
  always @(*)
    if (rst_n) begin
      assert(f_fetch || state == S_IDLE || state == S_DRAIN);
      assert(busy == (state != S_IDLE));
      assert(!(busy && done));
      assert(rd_req == (state == S_MAP || state == S_CLUT || state == S_FB));
      if (f_fetch) begin
        assert(w != 0 && h != 0);
        assert(c < row_tiles);
      end
    end

  // rgb444 sends pixel pairs: the sentinel is at an even bit, so a shift by
  // 2 never skips it
  function f_even(input [8:0] v);
    f_even = v[8] ? 1'b1 : v[7] ? 1'b0 : v[6] ? 1'b1 : v[5] ? 1'b0 :
             v[4] ? 1'b1 : v[3] ? 1'b0 : v[2] ? 1'b1 : v[1] ? 1'b0 : v[0];
  endfunction
  always @(*)
    if (rst_n && fmt == F_RGB444) begin
      if (busy)
        assert(!w[0]);
      assert(f_even(sh));
      if (h_valid)
        assert(f_even(h_sh));
    end

  // handoff register and output: the sentinel is always there, the byte
  // select is one-hot, the skid buffer fills after the head
  always @(*)
    if (rst_n) begin
      assert(sh != 0);
      if (h_valid)
        assert(h_sh[8:1] != 0);
      assert(bsel == 3'b001 || bsel == 3'b010 || bsel == 3'b100);
      if (fmt == F_RGB565)
        assert(!bsel[2]);
      if (sh_empty)
        assert(bsel == 3'b001);
      // idle: everything sent
      if (state == S_IDLE)
        assert(!h_valid && sh_empty && !byte_valid && !skid_valid);
      if (skid_valid)
        assert(byte_valid);
    end

  // the handoff register waits with an empty output only in the cycle after
  // it was put: the output takes it at once. so in S_DRAIN an empty output
  // means an empty handoff register, and DONE needs no !h_valid
  always @(*)
    if (rst_n && h_valid && sh_empty)
      assert(state == S_NEXT);

  // requests that complete
  reg [1:0]  f_fmt;
  reg [11:0] f_w, f_h;
  always @(posedge clk)
    if (start && !busy) begin
      f_fmt <= pwdata[2:1];
      f_w   <= w;
      f_h   <= h;
    end
  always @(posedge clk)
    if (f_run && done && !$past(done)) begin
      cover(f_fmt == F_RGB565 && f_w[0] && f_h == 2);
      cover(f_fmt == F_RGB444 && f_w == 10 && f_h == 2);
      cover(f_fmt == F_RGB666 && f_w == 3 && f_h == 1);
      cover(f_w == 1 && f_h == 9);    // two tile rows
    end
`endif

endmodule
