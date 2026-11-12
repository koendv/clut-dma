
#ifndef U8X8_CLUT_H
#define U8X8_CLUT_H

#define U8X8_CLUT_ENTRY_CNT 16
#define U8X8_CLUT_NATIVE_RGB444 1
#define U8X8_CLUT_NATIVE_RGB565 2

/* color lookup table, native rgb444 or rgb565. [idx][0] = foreground, [idx][1] = background */
typedef uint16_t clut_t[U8X8_CLUT_ENTRY_CNT][2];

/* color lookup table, 24bpp. [idx][0] = foreground, [idx][1] = background */
typedef uint32_t clut_rgb888_t[U8X8_CLUT_ENTRY_CNT][2];

/* size of u8x8->tile_clut_map[] */
#define u8x8_clut_map_size(pixel_width, pixel_height) \
  ((((((uint16_t)(pixel_width)+7)/8) * (((uint16_t)(pixel_height)+7)/8)) + 1) / 2)

#endif /* U8X8_CLUT_H */
