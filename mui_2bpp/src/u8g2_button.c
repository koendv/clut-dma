/*

  u8g2_button.c

  Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

  Copyright (c) 2016, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, 
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list 
    of conditions and the following disclaimer.
    
  * Redistributions in binary form must reproduce the above copyright notice, this 
    list of conditions and the following disclaimer in the documentation and/or other 
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  

*/

/*
  mui_2bpp: u8g2_DrawButtonFrame() and u8g2_DrawButtonUTF8() of u8g2_button.c,
  u8g2 branch clut, commit 07676acf: the two button functions mui_u8g2.c
  calls. The rest of u8g2_button.c is left out.

  u8g2_DrawButtonUTF8() is unchanged. u8g2_DrawButtonFrame() draws nothing:
  the bar of a field, in the place and size u8g2 gives it (text, padding,
  ascent and descent of the font), is a color of its tiles:
    U8G2_BTN_INV                  the inverted role (the focus)
    U8G2_BTN_INV, U8G2_BTN_XFRAME the edit role (a value being edited)
    a border (U8G2_BTN_BW_MASK)   the button role (the fi style)
  Shadows, frames and the inverting box of u8g2 are left out.
  u8g2_GetBarRows(): the tile rows of a bar, with the arithmetic of
  u8g2_DrawButtonFrame(), for a partial update (u8g2_FirstRows()).
*/

#include "u8g2.h"

/* the pixel rows of the bar of a field with baseline y: top and height */
static void u8g2_bar_rows(u8g2_t *u8g2, u8g2_uint_t y, u8g2_uint_t padding_v, u8g2_uint_t *top, u8g2_uint_t *h)
{
  int8_t a = u8g2_GetAscent(u8g2);
  int8_t d = u8g2_GetDescent(u8g2);

  *top = (u8g2_uint_t)(y + u8g2->font_calc_vref(u8g2) - a - padding_v);
  *h = (u8g2_uint_t)(a - d + 2 * padding_v);
}

void u8g2_DrawButtonFrame(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t flags, u8g2_uint_t text_width, u8g2_uint_t padding_h, u8g2_uint_t padding_v)
{
  u8g2_uint_t top, h;
  uint8_t role;

  if ( flags & U8G2_BTN_INV )
    role = (flags & U8G2_BTN_XFRAME) ? U8G2_ROLE_EDIT : U8G2_ROLE_INVERTED;
  else if ( flags & U8G2_BTN_BW_MASK )
    role = U8G2_ROLE_BUTTON;
  else
    return;
  u8g2_bar_rows(u8g2, y, padding_v, &top, &h);
  u8g2_MarkBar(u8g2, x - padding_h, top, text_width + 2 * padding_h, h, role);
}

/* the tile rows u8g2_DrawButtonFrame() marks for a bar with baseline y, in
   the current font; none: *r0 > *r1 */
void u8g2_GetBarRows(u8g2_t *u8g2, u8g2_uint_t y, u8g2_uint_t padding_v, uint8_t *r0, uint8_t *r1)
{
  u8g2_uint_t top, h;

  u8g2_bar_rows(u8g2, y, padding_v, &top, &h);
  u8g2_GetTileRows(u8g2, top, h, r0, r1);
}

void u8g2_DrawButtonUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t flags, u8g2_uint_t width, u8g2_uint_t padding_h, u8g2_uint_t padding_v, const char *text)
{
  u8g2_uint_t w = u8g2_GetUTF8Width(u8g2, text);
  
  u8g2_uint_t text_x_offset = 0; 

  if ( flags & U8G2_BTN_HCENTER )
    x -= (w+1)/2;

  if ( w < width )
  {
    if ( flags & U8G2_BTN_HCENTER )
    {
      text_x_offset = (width-w)/2;
    }
    w = width;
  }
  
  u8g2_SetFontMode(u8g2, 1);    
  u8g2_DrawUTF8(u8g2, x,y, text);
  u8g2_DrawButtonFrame(u8g2, x-text_x_offset, y, flags, w, padding_h, padding_v);
  
}
