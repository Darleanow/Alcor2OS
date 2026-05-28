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

void blit_cell_data(const fb_cell_t *c, int col, int row)
{
  u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
  u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

  u32 px_x = (u32)fb_ctx.margin_x + (u32)col * (u32)fb_ctx.cell_w;
  u32 px_y = (u32)fb_ctx.margin_y + (u32)row * (u32)fb_ctx.cell_h;

  if(fb_ctx.atlas_active) {
    if(fb_ctx.bytes_pp == 4) {
      u32 bg_pk = BGRA_OPAQUE_ALPHA | eff_bg;
      u32 fg_pk = BGRA_OPAQUE_ALPHA | eff_fg;

      if(c->cp == ' ') {
        for(u32 gy = 0; gy < (u32)fb_ctx.cell_h; gy++) {
          volatile u32 *dst =
              (volatile u32 *)(fb_ctx.base + (u64)(px_y + gy) * fb_ctx.pitch +
                               (u64)px_x * 4u);
          fill32(dst, bg_pk, (u32)fb_ctx.cell_w);
        }
        goto post;
      }

      u32 idx = atlas_lookup_attr(c->cp, c->attr);
      if(idx != ATLAS_NO_GLYPH && idx < fb_ctx.atlas_n_glyphs) {
        u32 fg_r = (eff_fg >> 16) & 0xffu, fg_g = (eff_fg >> 8) & 0xffu,
            fg_b = eff_fg & 0xffu;
        u32 bg_r = (eff_bg >> 16) & 0xffu, bg_g = (eff_bg >> 8) & 0xffu,
            bg_b = eff_bg & 0xffu;
        const u8 *glyph =
            fb_ctx.atlas_pixels + (size_t)idx * (size_t)fb_ctx.atlas_cell_h *
                                      (size_t)fb_ctx.atlas_stride;
        u32 atlas_bypp = (fb_ctx.atlas_bpp + 7u) / 8u;
        u32 cell_h     = fb_ctx.atlas_cell_h < (u32)fb_ctx.cell_h
                             ? fb_ctx.atlas_cell_h
                             : (u32)fb_ctx.cell_h;
        u32 cell_w     = fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w
                             ? fb_ctx.atlas_cell_w
                             : (u32)fb_ctx.cell_w;

        /* Fill the whole cell with bg first: when the atlas glyph is smaller
         * than the cell, the unblended margin would otherwise keep stale pixels
         * (e.g. the inverted cursor block), showing as artefacts. */
        if(cell_w < (u32)fb_ctx.cell_w || cell_h < (u32)fb_ctx.cell_h) {
          for(u32 gy = 0; gy < (u32)fb_ctx.cell_h; gy++) {
            volatile u32 *cell_row =
                (volatile u32 *)(fb_ctx.base + (u64)(px_y + gy) * fb_ctx.pitch +
                                 (u64)px_x * 4u);
            fill32(cell_row, bg_pk, (u32)fb_ctx.cell_w);
          }
        }

        for(u32 gy = 0; gy < cell_h; gy++) {
          const u8     *src = glyph + (size_t)gy * (size_t)fb_ctx.atlas_stride;
          volatile u32 *dst =
              (volatile u32 *)(fb_ctx.base + (u64)(px_y + gy) * fb_ctx.pitch +
                               (u64)px_x * 4u);
          blend_glyph_row(
              dst, src, cell_w, atlas_bypp, fg_r, fg_g, fg_b, bg_r, bg_g, bg_b,
              fg_pk, bg_pk
          );
        }
        goto post;
      }
    } else {
      u32 idx = atlas_lookup_attr(c->cp, c->attr);
      if(idx != ATLAS_NO_GLYPH && idx < fb_ctx.atlas_n_glyphs) {
        u32 fg_r = (eff_fg >> 16) & 0xffu, fg_g = (eff_fg >> 8) & 0xffu,
            fg_b = eff_fg & 0xffu;
        u32 bg_r = (eff_bg >> 16) & 0xffu, bg_g = (eff_bg >> 8) & 0xffu,
            bg_b = eff_bg & 0xffu;
        const u8 *glyph =
            fb_ctx.atlas_pixels + (size_t)idx * (size_t)fb_ctx.atlas_cell_h *
                                      (size_t)fb_ctx.atlas_stride;
        u32 atlas_bypp = (fb_ctx.atlas_bpp + 7u) / 8u;
        u32 cell_h     = fb_ctx.atlas_cell_h < (u32)fb_ctx.cell_h
                             ? fb_ctx.atlas_cell_h
                             : (u32)fb_ctx.cell_h;
        u32 cell_w     = fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w
                             ? fb_ctx.atlas_cell_w
                             : (u32)fb_ctx.cell_w;
        for(u32 gy = 0; gy < cell_h; gy++) {
          const u8 *src = glyph + (size_t)gy * (size_t)fb_ctx.atlas_stride;
          for(u32 gx = 0; gx < cell_w; gx++) {
            const u8 *px = src + (size_t)gx * atlas_bypp;
            u32       a  = (atlas_bypp == 4u) ? (u32)px[3] : (u32)px[0];
            if(!a) {
              fb_put_pixel(px_x + gx, px_y + gy, eff_bg);
              continue;
            }
            if(a == 255u) {
              fb_put_pixel(px_x + gx, px_y + gy, eff_fg);
              continue;
            }
            u32 inv = 255u - a;
            u32 r   = (fg_r * a + bg_r * inv + 128u) >> 8;
            u32 g   = (fg_g * a + bg_g * inv + 128u) >> 8;
            u32 b   = (fg_b * a + bg_b * inv + 128u) >> 8;
            fb_put_pixel(px_x + gx, px_y + gy, (r << 16) | (g << 8) | b);
          }
        }
        goto post;
      }
    }
  }

  for(int gy = 0; gy < fb_ctx.cell_h; gy++)
    for(int gx = 0; gx < fb_ctx.cell_w; gx++)
      fb_put_pixel(px_x + (u32)gx, px_y + (u32)gy, eff_bg);
  {
    u32 cp        = c->cp;
    u8  glyph_idx = (cp <= 0xffu) ? (u8)cp : (u8)'?';
    int gi        = font_glyph_index(glyph_idx);
    if(gi < 0)
      gi = font_glyph_index((u8)'?');
    if(gi >= 0) {
      int gx_off = (fb_ctx.cell_w > FONT_W) ? (fb_ctx.cell_w - FONT_W) / 2 : 0;
      int gy_off = (fb_ctx.cell_h > FONT_H) ? (fb_ctx.cell_h - FONT_H) / 2 : 0;
      const u8 *glyph = font_latin1[gi];
      for(int gy = 0; gy < FONT_H && gy < fb_ctx.cell_h; gy++) {
        u8 bits = glyph[gy];
        for(int gx = 0; gx < FONT_W && gx < fb_ctx.cell_w; gx++)
          if((bits & (0x80u >> gx)) != 0)
            fb_put_pixel(
                px_x + (u32)(gx_off + gx), px_y + (u32)(gy_off + gy), eff_fg
            );
      }
    }
  }

post:
  if((c->attr & FB_ATTR_UNDERLINE) && fb_ctx.base && fb_ctx.bytes_pp == 4) {
    u32 uline_color = BGRA_OPAQUE_ALPHA | eff_fg;
    u32 uline_y     = px_y + (u32)fb_ctx.cell_h - 2u;
    for(u32 uy = uline_y; uy < px_y + (u32)fb_ctx.cell_h; uy++) {
      volatile u32 *dst =
          (volatile u32 *)(fb_ctx.base + (u64)uy * fb_ctx.pitch +
                           (u64)px_x * 4u);
      fill32(dst, uline_color, (u32)fb_ctx.cell_w);
    }
  }
}

void blit_cell(int col, int row)
{
  blit_cell_data(
      &fb_ctx.cells[(size_t)row * (size_t)fb_ctx.cols + (size_t)col], col, row
  );
}
