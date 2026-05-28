/**
 * @file src/kernel/drivers/fb_console/cell.c
 * @brief Cell-to-pixel rendering for the framebuffer console.
 *
 * Turns one @ref fb_cell_t into a rectangle of framebuffer pixels via the
 * userspace atlas (32-bpp fast path or per-pixel for other depths) or the
 * compiled-in CP437 bitmap fallback. Underline drawing piggybacks on the
 * same routine — it is part of the cell's visual contract, not a separate
 * pass.
 */

#include <alcor2/types.h>
#include <drivers/console/font.h>
#include <kernel/drivers/fb_console/internal.h>

/** @brief MSB-set seed mask for the left-to-right column scan over a CP437
 * glyph row. Each row is 8 bits; the scan shifts this right one bit per column
 * so the bitmap fallback reads as a sweep, not a magic constant. */
#define BIT_MSB_8 0x80u

/**
 * @brief Unpacked 0xRRGGBB channels — kept as a struct so the per-pixel slow
 * path can pass them by pointer and avoid re-extracting per loop iteration.
 */
typedef struct
{
  u32 r; /**< Red channel 0..255. */
  u32 g; /**< Green channel 0..255. */
  u32 b; /**< Blue channel 0..255. */
} rgb_channels_t;

/**
 * @brief Split a packed 0xRRGGBB word into its three 8-bit channels.
 *
 * One call site so every blit helper agrees on the channel layout — a
 * renderer that re-implemented the unpack inline could accumulate drift
 * across the atlas and bitmap paths.
 *
 * @param packed  0xRRGGBB.
 * @return Unpacked channels.
 */
static rgb_channels_t rgb_unpack(u32 packed)
{
  return (rgb_channels_t) {
      .r = (packed >> BGRA_RED_SHIFT) & BYTE_MASK,
      .g = (packed >> BGRA_GREEN_SHIFT) & BYTE_MASK,
      .b = packed & BYTE_MASK,
  };
}

/**
 * @brief Pointer to the start of pixel-x @p px_x on framebuffer row @p y in
 * the 32-bpp fast path.
 *
 * The row stride is @c fb_ctx.pitch (not @c width * 4) — easy to miscalc if
 * inlined at every call site.
 *
 * @param px_x  Pixel-x of the cell's left edge.
 * @param y     Framebuffer row in pixels.
 * @return Volatile u32 pointer to the row start, 4-byte aligned.
 */
static inline volatile u32 *fb_row_at(u32 px_x, u32 y)
{
  return (volatile u32 *)(fb_ctx.base + (u64)y * fb_ctx.pitch +
                          (u64)px_x * FB_BYTES_PER_PIXEL_32);
}

/**
 * @brief Bytes per atlas pixel, rounded up from @c atlas_bpp.
 *
 * The @c (bpp + 7) / 8 round-up math reads as "bytes per pixel" only when the
 * @ref BITS_PER_BYTE constant is in the expression — that's why this helper
 * exists rather than the raw division at every call site.
 *
 * @return Bytes per atlas pixel (1..4).
 */
static inline u32 atlas_pixel_stride(void)
{
  return (fb_ctx.atlas_bpp + BITS_PER_BYTE - 1u) / BITS_PER_BYTE;
}

/**
 * @brief Atlas cell width clamped to the live cell width.
 *
 * Atlases with cells smaller than @c fb_ctx.cell_w paint only @c atlas_cell_w
 * pixels per row; clamping here keeps the caller's loop bound symmetric.
 *
 * @return Effective glyph width in pixels.
 */
static inline u32 atlas_blit_width(void)
{
  return (fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w) ? fb_ctx.atlas_cell_w
                                                    : (u32)fb_ctx.cell_w;
}

/**
 * @brief Companion of @ref atlas_blit_width for the height axis.
 *
 * @return Effective glyph height in pixels.
 */
static inline u32 atlas_blit_height(void)
{
  return (fb_ctx.atlas_cell_h < (u32)fb_ctx.cell_h) ? fb_ctx.atlas_cell_h
                                                    : (u32)fb_ctx.cell_h;
}

/**
 * @brief Bg-fill every framebuffer row inside the cell at (@p px_x, @p px_y).
 *
 * Shared by the space fast path and the smaller-than-cell atlas pre-pass.
 * Without the pre-pass, the uncovered margin keeps stale pixels — the
 * inverted cursor block was observed to leak through.
 *
 * @param px_x   Cell left-edge in framebuffer pixels.
 * @param px_y   Cell top-edge in framebuffer pixels.
 * @param bg_pk  0xFF000000 | bg, pre-packed for @ref fill32.
 */
static void fill_cell_bg_32bpp(u32 px_x, u32 px_y, u32 bg_pk)
{
  for(u32 gy = 0; gy < (u32)fb_ctx.cell_h; gy++)
    fill32(fb_row_at(px_x, px_y + gy), bg_pk, (u32)fb_ctx.cell_w);
}

/**
 * @brief Resolve @p cp / @p attr to an atlas glyph base pointer, or NULL when
 * no glyph is registered.
 *
 * Centralises the @ref ATLAS_NO_GLYPH and out-of-bounds checks so both atlas
 * blit paths get the same fallback behaviour without duplicating the guard.
 *
 * @param cp    Cell codepoint.
 * @param attr  SGR attribute bits.
 * @return First byte of the glyph's pixel data, or NULL.
 */
static const u8 *atlas_glyph_base(u32 cp, u16 attr)
{
  u32 idx = atlas_lookup_attr(cp, attr);
  if(idx == ATLAS_NO_GLYPH || idx >= fb_ctx.atlas_n_glyphs)
    return NULL;
  return fb_ctx.atlas_pixels + (size_t)idx * (size_t)fb_ctx.atlas_cell_h *
                                   (size_t)fb_ctx.atlas_stride;
}

/**
 * @brief 32-bpp atlas blit — row-by-row alpha-blend with at most one bg fill.
 *
 * Pre-fills the cell with bg when the atlas glyph is smaller than the cell so
 * the uncovered margin doesn't keep stale pixels; otherwise every pixel is
 * overwritten as part of the blend so the pre-fill would be wasted work.
 *
 * @param c      Cell content.
 * @param px_x   Cell left-edge in framebuffer pixels.
 * @param px_y   Cell top-edge in framebuffer pixels.
 * @param eff_fg Effective foreground (post-reverse).
 * @param eff_bg Effective background (post-reverse).
 * @return @c true when the glyph was drawn; @c false on atlas miss.
 */
static bool blit_atlas_32bpp(
    const fb_cell_t *c, u32 px_x, u32 px_y, u32 eff_fg, u32 eff_bg
)
{
  u32 bg_pk = BGRA_OPAQUE_ALPHA | eff_bg;
  u32 fg_pk = BGRA_OPAQUE_ALPHA | eff_fg;

  if(c->cp == ' ') {
    fill_cell_bg_32bpp(px_x, px_y, bg_pk);
    return true;
  }

  const u8 *glyph = atlas_glyph_base(c->cp, c->attr);
  if(!glyph)
    return false;

  rgb_channels_t fg     = rgb_unpack(eff_fg);
  rgb_channels_t bg     = rgb_unpack(eff_bg);
  u32            bypp   = atlas_pixel_stride();
  u32            cell_w = atlas_blit_width();
  u32            cell_h = atlas_blit_height();

  if(cell_w < (u32)fb_ctx.cell_w || cell_h < (u32)fb_ctx.cell_h)
    fill_cell_bg_32bpp(px_x, px_y, bg_pk);

  for(u32 gy = 0; gy < cell_h; gy++) {
    const u8 *src = glyph + (size_t)gy * (size_t)fb_ctx.atlas_stride;
    blend_glyph_row(
        fb_row_at(px_x, px_y + gy), src, cell_w, bypp, fg.r, fg.g, fg.b, bg.r,
        bg.g, bg.b, fg_pk, bg_pk
    );
  }
  return true;
}

/**
 * @brief Blend one atlas pixel onto the framebuffer at (@p x, @p y).
 *
 * Used by @ref blit_atlas_slow's tight per-pixel loop. The two ATLAS_*_ALPHA
 * indices match the two @c bypp values the renderer supports, so picking
 * between them is a one-bit choice rather than a switch.
 *
 * @param px      Atlas pixel source.
 * @param x       Framebuffer x.
 * @param y       Framebuffer y.
 * @param bypp    Bytes per atlas pixel.
 * @param eff_fg  Effective foreground (post-reverse).
 * @param eff_bg  Effective background (post-reverse).
 * @param fg      Unpacked fg channels.
 * @param bg      Unpacked bg channels.
 */
static void blend_atlas_pixel(
    const u8 *px, u32 x, u32 y, u32 bypp, u32 eff_fg, u32 eff_bg,
    const rgb_channels_t *fg, const rgb_channels_t *bg
)
{
  u32 a = (bypp == FB_BYTES_PER_PIXEL_32) ? (u32)px[ATLAS_RGBA_ALPHA_BYTE]
                                          : (u32)px[ATLAS_GRAY_ALPHA_BYTE];
  if(!a) {
    fb_put_pixel(x, y, eff_bg);
    return;
  }
  if(a == ALPHA_OPAQUE) {
    fb_put_pixel(x, y, eff_fg);
    return;
  }
  u32 inv = ALPHA_OPAQUE - a;
  u32 r   = (fg->r * a + bg->r * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE;
  u32 g   = (fg->g * a + bg->g * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE;
  u32 b   = (fg->b * a + bg->b * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE;
  fb_put_pixel(x, y, (r << BGRA_RED_SHIFT) | (g << BGRA_GREEN_SHIFT) | b);
}

/**
 * @brief Atlas blit for non-32-bpp framebuffers — per-pixel marshalling via
 * @ref fb_put_pixel.
 *
 * Format-conversion cost dominates here, so the row-cache fast path used by
 * the 32-bpp variant is not worth it — per-pixel through the one marshalling
 * chokepoint keeps the format logic in exactly one place.
 *
 * @return @c true when the glyph was drawn; @c false on atlas miss.
 */
static bool blit_atlas_slow(
    const fb_cell_t *c, u32 px_x, u32 px_y, u32 eff_fg, u32 eff_bg
)
{
  const u8 *glyph = atlas_glyph_base(c->cp, c->attr);
  if(!glyph)
    return false;

  rgb_channels_t fg     = rgb_unpack(eff_fg);
  rgb_channels_t bg     = rgb_unpack(eff_bg);
  u32            bypp   = atlas_pixel_stride();
  u32            cell_w = atlas_blit_width();
  u32            cell_h = atlas_blit_height();

  for(u32 gy = 0; gy < cell_h; gy++) {
    const u8 *src = glyph + (size_t)gy * (size_t)fb_ctx.atlas_stride;
    for(u32 gx = 0; gx < cell_w; gx++)
      blend_atlas_pixel(
          src + (size_t)gx * bypp, px_x + gx, px_y + gy, bypp, eff_fg, eff_bg,
          &fg, &bg
      );
  }
  return true;
}

/**
 * @brief CP437 bitmap fallback — bg-fill the cell, then plot the set bits of
 * the 8x16 font glyph centred inside the cell.
 *
 * Used when no atlas is registered or the codepoint has no atlas glyph. Falls
 * back to @c '?' when the codepoint itself has no CP437 mapping (codepoints
 * past 0xff or holes in the table).
 *
 * @param cp      Codepoint to render.
 * @param px_x    Cell left-edge in framebuffer pixels.
 * @param px_y    Cell top-edge in framebuffer pixels.
 * @param eff_fg  Effective foreground.
 * @param eff_bg  Effective background.
 */
static void
    blit_bitmap_glyph(u32 cp, u32 px_x, u32 px_y, u32 eff_fg, u32 eff_bg)
{
  for(int gy = 0; gy < fb_ctx.cell_h; gy++)
    for(int gx = 0; gx < fb_ctx.cell_w; gx++)
      fb_put_pixel(px_x + (u32)gx, px_y + (u32)gy, eff_bg);

  u8  glyph_idx = (cp <= BYTE_MASK) ? (u8)cp : (u8)'?';
  int gi        = font_glyph_index(glyph_idx);
  if(gi < 0)
    gi = font_glyph_index((u8)'?');
  if(gi < 0)
    return;

  int gx_off      = (fb_ctx.cell_w > FONT_W) ? (fb_ctx.cell_w - FONT_W) / 2 : 0;
  int gy_off      = (fb_ctx.cell_h > FONT_H) ? (fb_ctx.cell_h - FONT_H) / 2 : 0;
  const u8 *glyph = font_latin1[gi];
  for(int gy = 0; gy < FONT_H && gy < fb_ctx.cell_h; gy++) {
    u8 bits = glyph[gy];
    for(int gx = 0; gx < FONT_W && gx < fb_ctx.cell_w; gx++)
      if((bits & (BIT_MSB_8 >> gx)) != 0)
        fb_put_pixel(
            px_x + (u32)(gx_off + gx), px_y + (u32)(gy_off + gy), eff_fg
        );
  }
}

/**
 * @brief Paint the SGR underline bar at the bottom of the cell — 32-bpp only.
 *
 * The slow per-pixel path does not implement underline because no shipped
 * configuration uses a non-32-bpp framebuffer; gating here keeps the
 * underline math out of @ref blit_atlas_slow entirely.
 *
 * @param px_x    Cell left-edge.
 * @param px_y    Cell top-edge.
 * @param eff_fg  Effective foreground.
 */
static void blit_underline_32bpp(u32 px_x, u32 px_y, u32 eff_fg)
{
  u32 uline_color = BGRA_OPAQUE_ALPHA | eff_fg;
  u32 uline_y     = px_y + (u32)fb_ctx.cell_h - UNDERLINE_THICKNESS_PX;
  for(u32 uy = uline_y; uy < px_y + (u32)fb_ctx.cell_h; uy++)
    fill32(fb_row_at(px_x, uy), uline_color, (u32)fb_ctx.cell_w);
}

/**
 * @brief Render @p c at grid (col, row).
 *
 * Tries the first applicable rendering path — 32-bpp atlas, non-32-bpp atlas,
 * or the CP437 bitmap fallback — then overlays the underline bar when SGR
 * underline is on.
 *
 * @param c    Cell content.
 * @param col  Grid column.
 * @param row  Grid row.
 */
void blit_cell_data(const fb_cell_t *c, int col, int row)
{
  u32  eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
  u32  eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;
  u32  px_x   = (u32)fb_ctx.margin_x + (u32)col * (u32)fb_ctx.cell_w;
  u32  px_y   = (u32)fb_ctx.margin_y + (u32)row * (u32)fb_ctx.cell_h;

  bool drawn = false;
  if(fb_ctx.atlas_active) {
    if(fb_ctx.bytes_pp == FB_BYTES_PER_PIXEL_32)
      drawn = blit_atlas_32bpp(c, px_x, px_y, eff_fg, eff_bg);
    else
      drawn = blit_atlas_slow(c, px_x, px_y, eff_fg, eff_bg);
  }
  if(!drawn)
    blit_bitmap_glyph(c->cp, px_x, px_y, eff_fg, eff_bg);

  if((c->attr & FB_ATTR_UNDERLINE) && fb_ctx.base &&
     fb_ctx.bytes_pp == FB_BYTES_PER_PIXEL_32)
    blit_underline_32bpp(px_x, px_y, eff_fg);
}

/**
 * @brief Render the cell at the live grid position (col, row).
 *
 * Thin wrapper over @ref blit_cell_data — exists so callers that already own
 * grid coordinates don't repeat the @c row * @c cols + @c col index math.
 *
 * @param col  Grid column.
 * @param row  Grid row.
 */
void blit_cell(int col, int row)
{
  blit_cell_data(
      &fb_ctx.cells[(size_t)row * (size_t)fb_ctx.cols + (size_t)col], col, row
  );
}
