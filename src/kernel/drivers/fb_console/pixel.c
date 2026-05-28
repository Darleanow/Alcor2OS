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

/** @brief Bit offset of the red component inside a 16-bit RGB565 word.
 * R takes the top 5 bits of the 16-bit value. */
#define RGB565_R_SHIFT 11

/** @brief Bit offset of the green component inside a 16-bit RGB565 word. */
#define RGB565_G_SHIFT 5

/** @brief Number of low bits dropped to compress an 8-bit red channel into
 * the 5-bit slot inside RGB565. */
#define RGB565_R_LOSS 3

/** @brief Number of low bits dropped to compress an 8-bit green channel into
 * the 6-bit slot inside RGB565. */
#define RGB565_G_LOSS 2

/** @brief Number of low bits dropped to compress an 8-bit blue channel into
 * the 5-bit slot inside RGB565. */
#define RGB565_B_LOSS 3

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
