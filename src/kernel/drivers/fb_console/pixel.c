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

/* RGB565 packing: R takes the top 5 bits, G the next 6, B the bottom 5.
 * Named so the shifts in fb_put_pixel read as "build the 565 word", not
 * "shuffle bits by magic offsets". */
#define RGB565_R_SHIFT 11
#define RGB565_G_SHIFT 5
#define RGB565_R_LOSS  3 /* 8-bit → 5-bit drop. */
#define RGB565_G_LOSS  2 /* 8-bit → 6-bit drop. */
#define RGB565_B_LOSS  3 /* 8-bit → 5-bit drop. */

/* Byte positions inside a 32-bit packed pixel; written this way so the green
 * and red splits don't appear as bare shifts in the colour math. */
#define BGRA_GREEN_SHIFT 8
#define BGRA_RED_SHIFT   16

/**
 * @brief Translate the framebuffer's bits-per-pixel into bytes-per-pixel.
 *
 * Centralised so every code path that strides through a scanline agrees on
 * the same value; the @c default falls back to 4 because Limine's preferred
 * format is 32 bpp and assuming it keeps the renderer alive if the bootloader
 * ever reports an unfamiliar mode.
 *
 * @param bpp  Bits per pixel reported by the framebuffer request.
 * @return Bytes per pixel (1..4 in practice).
 */
u8 bytes_pp_from_bpp(u16 bpp)
{
  switch(bpp) {
  case FB_BPP_32:
    return FB_BYTES_PER_PIXEL_32;
  case FB_BPP_24:
    return FB_BYTES_PER_PIXEL_24;
  case FB_BPP_16:
    return FB_BYTES_PER_PIXEL_16;
  default:
    return FB_BYTES_PER_PIXEL_32;
  }
}

/**
 * @brief Plot one pixel honouring the current framebuffer depth.
 *
 * Clips silently against (@c width, @c height) at the only place where a
 * pixel actually lands so the dozens of callers (atlas glyphs, mouse halo,
 * cell underline) need not each defend against out-of-bounds coordinates.
 * The 32-bpp path forces the alpha byte to opaque so an alpha-aware
 * compositor downstream doesn't see ghost pixels from previous frames.
 *
 * @param x      Pixel column.
 * @param y      Pixel row.
 * @param color  0xRRGGBB; alpha is supplied by the renderer per format.
 */
void fb_put_pixel(u32 x, u32 y, u32 color)
{
  if(!fb_ctx.base || x >= fb_ctx.width || y >= fb_ctx.height)
    return;
  volatile u8 *p =
      fb_ctx.base + (u64)y * fb_ctx.pitch + (u64)x * fb_ctx.bytes_pp;
  switch(fb_ctx.bytes_pp) {
  case FB_BYTES_PER_PIXEL_32:
    *(volatile u32 *)p = color | BGRA_OPAQUE_ALPHA;
    return;
  case FB_BYTES_PER_PIXEL_24:
    p[0] = color & BYTE_MASK;
    p[1] = (color >> BGRA_GREEN_SHIFT) & BYTE_MASK;
    p[2] = (color >> BGRA_RED_SHIFT) & BYTE_MASK;
    return;
  case FB_BYTES_PER_PIXEL_16: {
    u32 r      = (color >> BGRA_RED_SHIFT) & BYTE_MASK;
    u32 g      = (color >> BGRA_GREEN_SHIFT) & BYTE_MASK;
    u32 b      = color & BYTE_MASK;
    u16 rgb565 = ((r >> RGB565_R_LOSS) << RGB565_R_SHIFT) |
                 ((g >> RGB565_G_LOSS) << RGB565_G_SHIFT) |
                 (b >> RGB565_B_LOSS);
    p[0] = rgb565 & BYTE_MASK;
    p[1] = rgb565 >> BGRA_GREEN_SHIFT;
    return;
  }
  default:
    return;
  }
}
