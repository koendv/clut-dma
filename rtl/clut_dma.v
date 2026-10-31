// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Koen De Vleeschauwer
//
// clut_dma: color expansion of a 1bpp frame buffer with a per-tile color
// lookup table, in a DMA engine. The deliverable of this repository; the
// FPGA builds in fpga/ validate and demonstrate it. In RGB565 and RGB444 the
// same output as u8x8_clut_stream_rgb565() and u8x8_clut_stream_rgb444() in
// u8g2 (csrc/u8x8_clut.c), for a frame buffer in horizontal or vertical layout.
//
// Memory, little-endian:
//   bitmap  1 bit per pixel, horizontal: a byte is 8 pixels of a pixel row,
//           the leftmost pixel in bit 7. as u8g2_ll_hvline_horizontal_right_lsb
//           in u8g2, the 1-bit buffers of LVGL and Adafruit GFX. no alignment.
//           or vertical (CTRL VERT), the tile layout of u8g2: a tile is 8
//           bytes, byte j the pixel column j, bit r the pixel row r. as
//           u8g2_ll_hvline_vertical_top_lsb. 8-byte aligned.
//   map     tile clut map: 4-bit clut index per 8x8 tile, two per byte, the
//           even tile in the low nibble. the map has as many tiles per tile
//           row as the bitmap.
//   CLUT    16 entries of 32 bits: foreground in bits 15:0, background in
//           bits 31:16. rgb565, or rgb444 in bits 11:0. 64-byte aligned.
//
// Request: a rectangle of W by H pixels, top left on a tile boundary. The cpu
// writes
//   FB_START     the bitmap address of the first tile: horizontal the byte of
//                the top left pixel, vertical the first byte of the top left
//                tile (bits 2:0 ignored)
//   MAP_START    the byte of the map that holds the entry of the top left
//                tile
//   MAP_NIBBLE   which nibble of that byte: 0 low, 1 high
//                with CTRL NO_MAP the map is not read: every tile has clut
//                entry 0, MAP_START and MAP_NIBBLE are not used. for a plain
//                bitmap, the clut is the one word of entry 0, 64-byte aligned
//   LINE_STRIDE  bitmap bytes from a line to the next: horizontal from a pixel
//                row to the next (TILE_COLS), vertical from a tile row to the
//                next (8 * TILE_COLS). the map steps LINE_STRIDE (horizontal)
//                or LINE_STRIDE / 8 (vertical) entries per tile row
//   SIZE         W and H in pixels. W = 8 * W[11:3] + W[2:0]: W[11:3] full
//                tiles and a last tile of W[2:0] pixels. for rgb444 W even
// The rectangle is sent pixel row by pixel row; the window of the display is
// set to the rectangle beforehand.
// The tile c of a line is at FB_START + c (horizontal) or FB_START + 8 * c
// (vertical), its map entry at MAP_START + c nibbles: an adder each, that in
// the last cycle of a tile (S_NEXT) steps FB_START or MAP_START to the next
// line instead. FB_START steps by LINE_STRIDE after every pixel row
// (horizontal) or tile row (vertical), MAP_START after every tile row.
// As in a DMA channel, the engine updates FB_START, MAP_START, MAP_NIBBLE and
// H during the transfer; write them again for the next request.
//
// Output: a stream of bytes, valid/ready, for a byte-wide peripheral (SPI).
// Bytes as u8x8_clut_stream_rgb565/444() send them:
//   rgb565: 2 bytes per pixel, most significant byte first.
//   rgb444: 3 bytes per two pixels, {pixel0, pixel1} most significant byte first.
//   rgb666: 3 bytes per pixel, r, g, b, 8 bits each: the rgb565 clut color
//           widened to 8 bits by replicating the most significant bits.
//           18-bit displays use bits 7:2, 24-bit (rgb888) displays all 8.
// Two stages, working in parallel:
//   fetch:  addresses and memory reads. per tile and pixel row the clut index,
//           foreground and background, and the pixels of the tile in that row,
//           into the handoff register. starts on the next tile as soon as the
//           output has taken the handoff register.
//   output: from the handoff register, the bytes of up to 8 pixels.
// so the memory reads of a tile overlap the output of the previous tile.
// The fetch keeps the last map word, the last bitmap word, and the clut index
// of the colors in the handoff register. It reads a map word at a word
// boundary of the map, a bitmap word at a word boundary, both also at the
// first tile of a pixel row, and a clut word when the index changes.
// Vertical: the 2 bitmap words of every tile in every pixel row, keeping bit
// r of each byte, the pixels of the tile in pixel row r; map and clut as
// horizontal. A change in memory during a transfer may take effect only at
// the next read of that word.
// Parameter CACHE = 0: no kept words. Every tile reads the map word, the clut
// word and the bitmap word (vertical: 2 words), and keeps only the nibble
// and the bits it needs.
// The same output, more reads; to tell a fault in the cache logic from one
// elsewhere.
// A 2-byte skid buffer between output and peripheral.
// DONE is set when the last byte has left the buffer.
// Bus error: a read with rd_err. The engine makes no more reads; the bytes
// already in the output still go out, then ERR is set instead of DONE. The
// display has received only part of the rectangle.
//
// Register interface, for a bus adapter (APB3, AHB-Lite) outside this module:
//   reg_wr     one cycle: write reg_wdata to the register at reg_addr, in
//              this cycle
//   reg_rd     one cycle: read the register at reg_addr; reg_rdata is valid
//              in the next cycle, and stays until the next reg_rd
//   reg_addr   bits 4:2 of the byte offset below
// No wait states and no errors.
//
// Registers, byte offsets:
//   0x00 CTRL         bit 0 START (write 1),
//                     bits 2:1 FORMAT (0 rgb565, 1 rgb444, 2 rgb666, 3 reserved),
//                     bit 3 IRQ_EN,
//                     bit 4 VERT, bitmap in vertical layout
//                     bit 5 NO_MAP, no tile map: all tiles clut entry 0
//   0x04 STATUS       bit 0 BUSY, bit 1 DONE (write 1 to clear),
//                     bit 2 ERR, bus error (write 1 to clear)
//   0x08 FB_START     bitmap address of the first tile; vertical: bits 2:0 ignored
//   0x0C MAP_START    map byte of the entry of the first tile
//   0x10 CLUT         64-byte aligned: bits 5:0 are ignored and read as 0
//   0x14 LINE_STRIDE  bits 15:0, bitmap bytes from a line to the next
//   0x18 MAP_NIBBLE   bit 0, the nibble of MAP_START: 0 low, 1 high
//   0x1C SIZE         bits 11:0 W, bits 27:16 H, in pixels
//
// All outputs come from flip-flops.

module clut_dma #(
  parameter CACHE = 1             // keep the last map, bitmap and clut word
) (
  input             clk,
  input             rst_n,

  // register port
  input             reg_wr,
  input             reg_rd,
  input      [4:2]  reg_addr,
  input      [31:0] reg_wdata,
  output reg [31:0] reg_rdata,
  output reg        irq,

  // memory read port: rd_addr is stable while rd_req is high,
  // rd_data is valid when rd_ack is high; rd_err with rd_ack: the read failed
  output reg        rd_req,
  output reg [31:0] rd_addr,
  input             rd_ack,
  input      [31:0] rd_data,
  input             rd_err,

  // byte stream
  output reg        byte_valid,
  output reg [7:0]  byte_data,
  input             byte_ready
);

  // fetch states
  localparam S_IDLE  = 4'd0;
  localparam S_ABORT = 4'd1;
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
  reg [31:0] fb_start;    // bitmap: the first tile of the line
  reg [32:0] map_start;   // map: nibble address of the first entry of the tile row
  reg [31:6] clut;        // color lookup table: address, aligned to multiple of 64
  reg [15:0] line_stride; // bitmap bytes from a line to the next
  reg [11:0] w, h;        // pixels; H counts the pixel rows left
  reg [1:0]  fmt;
  reg        vert;        // bitmap in the vertical tile layout of u8g2
  reg        no_map;      // no tile map: all tiles clut entry 0
  reg        irq_en, busy, done, err;

  // fetch
  reg [3:0]  state;
  reg [8:0]  c;        // tile in the line
  reg [2:0]  row;      // pixel row in the tile row
  reg [31:0] map_word; // last map word
  reg [31:0] fb_word;  // last bitmap word
  reg [3:0]  idx;      // clut index of the colors in h_fg, h_bg
  reg        clut_valid;
  // vertical: word of the tile being read, and bit r of each byte read so far
  reg        wcnt;
  reg [7:0]  vacc;

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

  wire start     = reg_wr && reg_addr == 3'd0 && reg_wdata[0];

  // the line: tiles c = 0 .. W-1. bitmap: FB_START + c (horizontal, a byte
  // per tile and pixel row) or FB_START + 8*c (vertical, a tile of 8 bytes);
  // map: MAP_START + c nibbles. FB_START and MAP_START step per line: one
  // adder each, the address of the tile or, in S_NEXT, the line step
  wire [8:0]  c_next   = c + 9'd1;
  wire        step     = state == S_NEXT;
  // W pixels: W[11:3] full tiles, and a last tile of W[2:0] pixels if not 0.
  // the last tile of the line: c = W[11:3] - 1, or c = W[11:3] with a
  // partial tile. no adder: compare c or c_next with W[11:3]
  wire        part_w   = w[2:0] != 3'd0;
  wire        last_tile = part_w ? (c == w[11:3]) : (c_next == w[11:3]);
  wire        line_done = last_tile;
  // H counts pixel rows: the last tile row may be partial
  wire        req_end  = step && h == 12'd1 && line_done;
  wire        row_end  = step && line_done && !req_end;   // end of a pixel row
  wire        grp_end  = row_end && row == 3'd7;           // end of a tile row
  wire        fb_line  = vert ? grp_end : row_end;
  wire [31:0] fb_a     = { fb_start[31:3], vert ? 3'b000 : fb_start[2:0] };   // vertical: 8-byte aligned
  wire [31:0] fb_sum   = fb_a + (step ? { 16'd0, line_stride }
                                      : vert ? { 20'd0, c, 3'b000 } : { 23'd0, c });
  wire [32:0] map_sum  = map_start + (step ? (vert ? { 20'd0, line_stride[15:3] } : { 17'd0, line_stride })
                                           : { 24'd0, c });
  wire [31:0] fbp      = fb_sum;
  wire [32:0] map_cur  = map_sum;
  wire [31:2] fb_rd    = fbp[31:2];

  // clut index of the tile, from the map word
  wire [3:0]  tile_idx  = no_map ? 4'd0 : map_word[{ map_cur[2:0], 2'b00 } +: 4];

  // reads for this tile: at the first tile of a line, at a word boundary,
  // when the clut index changes. without cache: always. vertical: the
  // bitmap words of every tile
  wire        need_map = !no_map && (!CACHE || c == 9'd0 || map_cur[2:0] == 3'd0);
  wire        need_fb  = !CACHE || vert || c == 9'd0 || fbp[1:0] == 2'd0;
  wire        need_clut = !CACHE || !clut_valid || tile_idx != idx;

  // without cache: the clut index and the bitmap byte straight from the read
  wire [3:0]  rd_idx   = rd_data[{ map_cur[2:0], 2'b00 } +: 4];
  wire [7:0]  rd_byte  = rd_data[{ fbp[1:0], 3'b000 } +: 8];

  // bitmap byte of the tile, from the bitmap word. leftmost pixel in bit 7,
  // reversed: leftmost in bit 0
  wire [7:0]  fb_byte  = CACHE ? fb_word[{ fbp[1:0], 3'b000 } +: 8] : fb_word[7:0];
  wire [7:0]  hpx      = { fb_byte[0], fb_byte[1], fb_byte[2], fb_byte[3],
                           fb_byte[4], fb_byte[5], fb_byte[6], fb_byte[7] };

  // vertical: bit r of each byte of the word just read, byte k in bit k. the
  // pixels: bit r of the 8 bytes of the tile; leftmost pixel (byte 0,
  // column 0) in bit 0
  wire [3:0]  rd_nib   = { rd_data[{ 2'd3, row }], rd_data[{ 2'd2, row }],
                           rd_data[{ 2'd1, row }], rd_data[{ 2'd0, row }] };
  wire [7:0]  px       = vert ? vacc : hpx;

  // pixels of the tile and the sentinel above the last one
  wire [3:0]  npx     = (part_w && last_tile) ? { 1'b0, w[2:0] } : 4'd8;
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
      F_RGB666: bytes = { pix0[15:11], pix0[15:13],
                          pix0[10:5],  pix0[10:9],
                          pix0[4:0],   pix0[4:2] };
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

  // the output and the skid buffer are empty
  wire drained = !h_valid && sh_empty && !byte_valid && !skid_valid;

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

  // registers
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      fb_start <= 0; map_start <= 0; clut <= 0;
      line_stride <= 0; w <= 0; h <= 0;
      fmt <= 0; vert <= 0; no_map <= 0; irq_en <= 0;
      reg_rdata <= 0;
      irq <= 0;
    end else begin
      // writes ignored while busy, except STATUS
      if (reg_wr) begin
        case (reg_addr)
          3'd0: if (!busy) begin
                  fmt    <= reg_wdata[2:1];
                  irq_en <= reg_wdata[3];
                  vert   <= reg_wdata[4];
                  no_map <= reg_wdata[5];
                end
          3'd2: if (!busy) fb_start <= reg_wdata;
          3'd3: if (!busy) map_start[32:1] <= reg_wdata;
          3'd4: if (!busy) clut <= reg_wdata[31:6];
          3'd5: if (!busy) line_stride <= reg_wdata[15:0];
          3'd6: if (!busy) map_start[0] <= reg_wdata[0];
          3'd7: if (!busy) begin
                  w <= reg_wdata[11:0];
                  h <= reg_wdata[27:16];
                end
          default: ;
        endcase
      end
      if (fb_line)
        fb_start <= fb_sum;
      if (grp_end)
        map_start <= map_sum;
      if (row_end || req_end)
        h <= h - 12'd1;
      if (reg_rd) begin
        case (reg_addr)
          3'd0:    reg_rdata <= { 26'd0, no_map, vert, irq_en, fmt, 1'b0 };
          3'd1:    reg_rdata <= { 29'd0, err, done, busy };
          3'd2:    reg_rdata <= fb_start;
          3'd3:    reg_rdata <= map_start[32:1];
          3'd4:    reg_rdata <= { clut, 6'd0 };
          3'd5:    reg_rdata <= { 16'd0, line_stride };
          3'd6:    reg_rdata <= { 31'd0, map_start[0] };
          default: reg_rdata <= { 4'd0, h, 4'd0, w };
        endcase
      end
      irq <= (done | err) & irq_en;
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
      busy <= 0; done <= 0; err <= 0;
      state <= S_IDLE;
      rd_req <= 0; rd_addr <= 0;
      c <= 0; row <= 0;
      map_word <= 0; fb_word <= 0; idx <= 0; clut_valid <= 0;
      wcnt <= 0; vacc <= 0;
      h_sh <= 0; h_fg <= 0; h_bg <= 0;
    end else begin

      if (reg_wr && reg_addr == 3'd1 && reg_wdata[1])
        done <= 0;
      if (reg_wr && reg_addr == 3'd1 && reg_wdata[2])
        err <= 0;

      case (state)
        S_IDLE:
          if (start) begin
            c     <= 0;
            row   <= 0;
            clut_valid <= 0;
            busy  <= 1;
            done  <= 0;
            err   <= 0;
            state <= (w == 0 || h == 0) ? S_DRAIN : S_WAIT;
          end

        // until the output has taken the handoff register. then the map
        // word if needed, else the clut index from the held map word
        S_WAIT:
          if (!h_valid) begin
            if (need_map) begin
              rd_req  <= 1;
              rd_addr <= { map_cur[32:3], 2'b00 };
              state   <= S_MAP;
            end else if (need_clut) begin
              idx     <= tile_idx;
              rd_req  <= 1;
              rd_addr <= { clut, tile_idx, 2'b00 };
              state   <= S_CLUT;
            end else if (need_fb) begin
              rd_req  <= 1;
              rd_addr <= { fb_rd[31:2], 2'b00 };
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
            rd_addr <= { fb_rd[31:2], 2'b00 };
            state   <= S_FB;
          end else
            state <= S_PUT;

        S_CLUT:
          if (rd_ack) begin
            h_fg       <= rd_data[15:0];
            h_bg       <= rd_data[31:16];
            clut_valid <= 1;
            if (need_fb) begin
              rd_addr <= { fb_rd[31:2], 2'b00 };
              state   <= S_FB;
            end else begin
              rd_req  <= 0;
              state   <= S_PUT;
            end
          end

        // vertical: the words of the tile, bit r of each byte into vacc
        S_FB:
          if (rd_ack) begin
            if (vert) begin
              vacc[{ wcnt, 2'b00 } +: 4] <= rd_nib;
              if (wcnt) begin
                wcnt   <= 0;
                rd_req <= 0;
                state  <= S_PUT;
              end else begin
                wcnt    <= 1;
                rd_addr <= { rd_addr[31:3], 1'b1, 2'b00 };
              end
            end else begin
              fb_word <= CACHE ? rd_data : { 24'd0, rd_byte };
              rd_req  <= 0;
              state   <= S_PUT;
            end
          end

        // the bitmap byte from the bitmap word, into the handoff register
        S_PUT:
          begin
            h_sh  <= px_sh;
            state <= S_NEXT;
          end

        // FB_START, MAP_START and H step in the register block
        S_NEXT:
          if (req_end)
            state <= S_DRAIN;
          else if (row_end) begin
            c     <= 0;
            row   <= row + 3'd1;
            state <= S_WAIT;
          end else begin
            c     <= c_next;
            state <= S_WAIT;
          end

        // done when the output is empty and the last byte has left the skid buffer
        S_DRAIN:
          if (drained) begin
            busy  <= 0;
            done  <= 1;
            state <= S_IDLE;
          end

        // after a bus error: the same, then ERR instead of DONE
        S_ABORT:
          if (drained) begin
            busy  <= 0;
            err   <= 1;
            state <= S_IDLE;
          end

        default:
          state <= S_IDLE;
      endcase

      // bus error: no more reads. overrides the read states above; what they
      // stored from rd_data is not used
      if (rd_ack && rd_err) begin
        rd_req <= 0;
        wcnt   <= 0;
        state  <= S_ABORT;
      end
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
    if (start && reg_wdata[2:1] == F_RGB444)
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
      assert(irq == $past((done || err) && irq_en));

  // fetch: legal states, busy outside S_IDLE, a read in the read states
  wire f_fetch = state == S_WAIT || state == S_MAP || state == S_CLUT ||
                 state == S_FB || state == S_IDX || state == S_PUT ||
                 state == S_NEXT;
  always @(*)
    if (rst_n) begin
      assert(f_fetch || state == S_IDLE || state == S_DRAIN || state == S_ABORT);
      assert(busy == (state != S_IDLE));
      assert(!(busy && done));
      assert(!(busy && err));
      assert(!(done && err));
      assert(rd_req == (state == S_MAP || state == S_CLUT || state == S_FB));
      // NO_MAP: no map reads
      assert(!(no_map && state == S_MAP));
      // the tile in the line: at most the last one, W[11:3] - 1, or W[11:3]
      // with a partial last tile
      if (f_fetch) begin
        assert(w != 0 && h != 0);
        assert(part_w ? c <= w[11:3] : c < w[11:3]);
      end
      // vertical: the word counter only in S_FB; the address the word of
      // the tile the counter is at, FB_START + 8 * c
      if (state != S_FB || !vert)
        assert(!wcnt);
      else
        assert(rd_addr == { fb_sum[31:3], wcnt, 2'b00 });
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

  // covers of one layout only, to keep the search short: the requests are
  // horizontal (ROW_COVER) or vertical (VERT_COVER). not in the proofs
`ifdef ROW_COVER
  always @(*)
    if (reg_wr && reg_addr == 3'd0)
      assume(!reg_wdata[4]);
`endif
`ifdef VERT_COVER
  always @(*)
    if (reg_wr && reg_addr == 3'd0)
      assume(reg_wdata[4]);
`endif

  // requests that complete
  reg [1:0]  f_fmt;
  reg        f_vert, f_nomap;
  reg [11:0] f_w, f_h;
  always @(posedge clk)
    if (start && !busy) begin
      f_fmt   <= reg_wdata[2:1];
      f_vert  <= reg_wdata[4];
      f_nomap <= reg_wdata[5];
      f_w     <= w;
      f_h     <= h;
    end
  always @(posedge clk)
    if (f_run && done && !$past(done)) begin
`ifndef VERT_COVER
      cover(!f_vert && f_fmt == F_RGB565 && f_w[0] && f_h == 2);
      cover(!f_vert && f_fmt == F_RGB444 && f_w == 10 && f_h == 2);
      cover(!f_vert && f_fmt == F_RGB666 && f_w == 3 && f_h == 1);
      cover(!f_vert && f_w == 1 && f_h == 9);    // two tile rows
      cover(!f_vert && f_nomap && f_w == 9 && f_h == 2);   // no map, two tiles
`else
      // vertical, the long covers (clut_dma.sby cover_vert): two tiles, two
      // tile rows
      cover(f_vert && f_fmt == F_RGB565 && f_w == 9 && f_h == 2);
      cover(f_vert && f_fmt == F_RGB444 && f_w == 10 && f_h == 1);
      cover(f_vert && f_fmt == F_RGB666 && f_w == 3 && f_h == 1);
      cover(f_vert && f_w == 1 && f_h == 9);
`endif
    end

  // a request that ends with a bus error
  always @(posedge clk)
    if (f_run)
      cover(err && !$past(err) && f_w != 0 && f_h != 0);
`endif

endmodule
