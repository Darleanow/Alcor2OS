/**
 * @file src/kernel/drivers/fb_console/pixel.c
 * @brief Raw framebuffer pixel primitives for the fb_console module.
 *
 * Pure pixel-level access: no awareness of cells, glyphs, or grid. Every
 * higher layer (cell, flush, mouse cursor) routes through these so the
 * per-bpp marshalling lives in exactly one place.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

u8 bytes_pp_from_bpp(u16 bpp)
{
  switch(bpp) {
  case 32:
    return 4;
  case 24:
    return 3;
  case 16:
    return 2;
  default:
    return 4;
  }
}

void fb_put_pixel(u32 x, u32 y, u32 color)
{
  if(!fb_ctx.base || x >= fb_ctx.width || y >= fb_ctx.height)
    return;
  volatile u8 *p =
      fb_ctx.base + (u64)y * fb_ctx.pitch + (u64)x * fb_ctx.bytes_pp;
  switch(fb_ctx.bytes_pp) {
  case 4:
    *(volatile u32 *)p = color | 0xFF000000u;
    return;
  case 3:
    p[0] = (u8)(color & 0xffu);
    p[1] = (u8)((color >> 8) & 0xffu);
    p[2] = (u8)((color >> 16) & 0xffu);
    return;
  case 2: {
    u32 r      = (color >> 16) & 0xffu;
    u32 g      = (color >> 8) & 0xffu;
    u32 b      = color & 0xffu;
    u16 rgb565 = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    p[0]       = (u8)(rgb565 & 0xffu);
    p[1]       = (u8)(rgb565 >> 8);
    return;
  }
  default:
    return;
  }
}
