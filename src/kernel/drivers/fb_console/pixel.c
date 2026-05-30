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
#include <drivers/console/font.h>

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

/** @brief Byte index of the blue channel in a little-endian 24-bit RGB
 * triplet (B is first byte, then G, then R). Named because the bare @c [0]
 * subscript reads as positional even when it's intentional. */
#define BGR24_B 0u

/** @brief Byte index of the green channel in a little-endian 24-bit RGB
 * triplet. */
#define BGR24_G 1u

/** @brief Byte index of the red channel in a little-endian 24-bit RGB
 * triplet. */
#define BGR24_R 2u

/** @brief Byte index of the low byte of an RGB565 word stored little-endian
 * in framebuffer memory. */
#define RGB565_LO 0u

/** @brief Byte index of the high byte of an RGB565 word stored little-endian
 * in framebuffer memory. */
#define RGB565_HI 1u

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
    p[BGR24_B] = color & BYTE_MASK;
    p[BGR24_G] = (color >> BGRA_GREEN_SHIFT) & BYTE_MASK;
    p[BGR24_R] = (color >> BGRA_RED_SHIFT) & BYTE_MASK;
    return;
  case FB_BYTES_PER_PIXEL_16: {
    u32 r      = (color >> BGRA_RED_SHIFT) & BYTE_MASK;
    u32 g      = (color >> BGRA_GREEN_SHIFT) & BYTE_MASK;
    u32 b      = color & BYTE_MASK;
    u16 rgb565 = ((r >> RGB565_R_LOSS) << RGB565_R_SHIFT) |
                 ((g >> RGB565_G_LOSS) << RGB565_G_SHIFT) |
                 (b >> RGB565_B_LOSS);
    p[RGB565_LO] = rgb565 & BYTE_MASK;
    p[RGB565_HI] = rgb565 >> BGRA_GREEN_SHIFT;
    return;
  }
  default:
    return;
  }
}

/**
 * @brief Reads a pixel's color from the framebuffer.
 *
 * Silently clips against (@c width, @c height) and returns 0 if out-of-bounds.
 *
 * @param x Pixel column.
 * @param y Pixel row.
 * @return The 0xRRGGBB color of the pixel, stripped of alpha.
 */
u32 fb_get_pixel(u32 x, u32 y)
{
  if(!fb_ctx.base || x >= fb_ctx.width || y >= fb_ctx.height)
    return 0;
  volatile u8 *p =
      fb_ctx.base + (u64)y * fb_ctx.pitch + (u64)x * fb_ctx.bytes_pp;
  switch(fb_ctx.bytes_pp) {
  case FB_BYTES_PER_PIXEL_32:
    return (*(volatile u32 *)p) & 0xFFFFFF;
  case FB_BYTES_PER_PIXEL_24:
    return ((u32)p[BGR24_B]) | ((u32)p[BGR24_G] << BGRA_GREEN_SHIFT) |
           ((u32)p[BGR24_R] << BGRA_RED_SHIFT);
  case FB_BYTES_PER_PIXEL_16: {
    u16 rgb565 = p[RGB565_LO] | ((u16)p[RGB565_HI] << BITS_PER_BYTE);
    u32 r      = (rgb565 >> RGB565_R_SHIFT) & 0x1F;
    u32 g      = (rgb565 >> RGB565_G_SHIFT) & 0x3F;
    u32 b      = rgb565 & 0x1F;
    return (r << (RGB565_R_LOSS + BGRA_RED_SHIFT)) |
           (g << (RGB565_G_LOSS + BGRA_GREEN_SHIFT)) | (b << RGB565_B_LOSS);
  }
  default:
    return 0;
  }
}

/**
 * @brief Draws a monochrome 8x16 glyph onto the framebuffer.
 *
 * Iterates through the 16 bytes of the glyph (one byte per row) and sets
 * the pixel to @p fg if the bit is 1, and @p bg if the bit is 0.
 *
 * @param x     Top-left X coordinate.
 * @param y     Top-left Y coordinate.
 * @param glyph Pointer to the 16-byte glyph data.
 * @param fg    Foreground color (0xRRGGBB).
 * @param bg    Background color (0xRRGGBB).
 */
void fb_draw_glyph(u32 x, u32 y, const u8 *glyph, u32 fg, u32 bg)
{
  if(!glyph)
    return;
  for(u32 row = 0; row < FONT_H; row++) {
    u8 row_data = glyph[row];
    for(u32 col = 0; col < FONT_W; col++) {
      u32 color = (row_data & (1 << (7 - col))) ? fg : bg;
      fb_put_pixel(x + col, y + row, color);
    }
  }
}

/**
 * @brief Draws a fallback character using the built-in VGA font.
 *
 * Looks up the character in the Latin-1 atlas. If not found, falls back to '?'.
 *
 * @param x  Top-left X coordinate.
 * @param y  Top-left Y coordinate.
 * @param c  The character to draw.
 * @param fg Foreground color.
 * @param bg Background color.
 */
void fb_draw_fallback_char(u32 x, u32 y, char c, u32 fg, u32 bg)
{
  int gi = font_glyph_index((u8)c);
  if(gi < 0 || gi >= (int)FONT_GLYPHS)
    gi = font_glyph_index((u8)'?');
  const u8 *glyph = font_latin1[gi];
  fb_draw_glyph(x, y, glyph, fg, bg);
}
