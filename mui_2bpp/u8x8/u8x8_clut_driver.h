#ifndef U8X8_CLUT_DRIVER_H
#define U8X8_CLUT_DRIVER_H

#include "u8x8.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef U8X8_WITH_CLUT

/* tile_clut_map is two 4 bit clut indices per byte, low nibble first. */
static inline void u8x8_clut_set_nibble(uint8_t *map, uint16_t index, uint8_t value)
{
  if (!map)
    return;
  uint8_t *ptr = map + (index >> 1);
  if ( index & 1 )
    *ptr = (uint8_t)((*ptr & 0x0f) | (value << 4));
  else
    *ptr = (uint8_t)((*ptr & 0xf0) | (value & 0x0f));
}

static inline uint8_t u8x8_clut_get_nibble(const uint8_t *map, uint16_t index)
{
  if (!map)
    return 0;
  uint8_t b = map[index >> 1];
  return (index & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0f);
}

/* color drivers, at setup: native format of the clut entries, rgb444 or rgb565 */
void u8x8_clut_set_native_bpp(u8x8_t *u8x8, uint8_t native_bpp);

/* without a table: white on black, native format */
const uint16_t *u8x8_clut_bw(const u8x8_t *u8x8);

/* send rgb565 to display */
void u8x8_clut_stream_rgb565(u8x8_t *u8x8,
    const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number);

/* send rgb444 to display */
void u8x8_clut_stream_rgb444(u8x8_t *u8x8,
    const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number);

#endif /* U8X8_WITH_CLUT */

#ifdef __cplusplus
}
#endif

#endif /* U8X8_CLUT_DRIVER_H */
