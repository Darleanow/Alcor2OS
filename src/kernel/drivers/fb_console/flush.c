/**
 * @file src/kernel/drivers/fb_console/flush.c
 * @brief Dirty-cell flush batch + character emission for the framebuffer
 * console.
 *
 * Two related responsibilities:
 *   - @ref put_cp_at_cursor — write one codepoint at the cell cursor, mark
 *     the cell dirty (in batch mode) or blit it immediately, and advance.
 *     Called once per glyph emitted by the ANSI/UTF-8 feeder.
 *   - @ref flush_batch — turn every cell whose @c dirty bit is set in the
 *     [@c batch_r0, @c batch_r1] row range into pixels, in one VRAM pass,
 *     using a heap-owned per-row scratch (@ref s_flush_ci) so we can avoid
 *     re-resolving the atlas glyph for every scanline of every cell.
 */

#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

struct flush_cell_cache
{
  const u8 *glyph_base; /* NULL = bg-only (space / blink-off) */
  u32       bg_pk;
  u32       fg_pk;
  u32       fg_r, fg_g, fg_b;
  u32       bg_r, bg_g, bg_b;
  bool      active;
  bool      underline;
};

/* Per-row scratch used by flush_batch. ~48 B per cell — at 160+ cols far too
 * large for the 8 KiB kernel stack, so heap-owned and grown on demand
 * whenever the grid widens. */
static struct flush_cell_cache *s_flush_ci      = NULL;
static int                      s_flush_ci_cols = 0;

/**
 * @brief Grow the per-row scratch to hold at least @p cols entries.
 *
 * Allocation failure leaves the previous buffer in place: @ref flush_batch
 * tolerates @c s_flush_ci being NULL but not smaller than the current grid,
 * so retaining the larger old buffer is preferable to shrinking on failure.
 *
 * @param cols  Minimum capacity in cells.
 */
void flush_ci_ensure(int cols)
{
  if(cols <= s_flush_ci_cols && s_flush_ci)
    return;
  struct flush_cell_cache *nb =
      kmalloc((size_t)cols * sizeof(struct flush_cell_cache));
  if(!nb)
    return;
  if(s_flush_ci)
    kfree(s_flush_ci);
  s_flush_ci      = nb;
  s_flush_ci_cols = cols;
}

/**
 * @brief Repaint every dirty cell in the @c [batch_r0, batch_r1] row range
 * in one VRAM pass.
 *
 * Fast path resolves each cell's atlas glyph into the per-row scratch once,
 * then walks scanline-by-scanline so the inner loop is a tight 4-byte fill
 * or alpha-blend. The slow path falls back to @ref blit_cell per cell — that
 * covers 24/16-bpp and the no-atlas state, neither of which is performance
 * critical.
 */
void flush_batch(void)
{
  if(!fb_ctx.cells || fb_ctx.batch_r0 > fb_ctx.batch_r1)
    return;
  int r0 = fb_ctx.batch_r0 < 0 ? 0 : fb_ctx.batch_r0;
  int r1 = fb_ctx.batch_r1 >= fb_ctx.rows ? fb_ctx.rows - 1 : fb_ctx.batch_r1;

  if(!fb_ctx.base || fb_ctx.bytes_pp != FB_BYTES_PER_PIXEL_32) {
    for(int r = r0; r <= r1; r++)
      for(int c = 0; c < fb_ctx.cols; c++) {
        fb_cell_t *cell =
            &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c];
        if(cell->dirty) {
          cell->dirty = 0;
          blit_cell(c, r);
        }
      }
    return;
  }

  u32 atlas_bypp = (fb_ctx.atlas_bpp + BITS_PER_BYTE - 1u) / BITS_PER_BYTE;
  u32 acw = (fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w) ? fb_ctx.atlas_cell_w
                                                       : (u32)fb_ctx.cell_w;

  /* Heap-owned scratch; sized at grid init and grown on reflow. The setup
   * loop below fills every column unconditionally, so we don't pre-zero. */
  if(!s_flush_ci || s_flush_ci_cols < fb_ctx.cols)
    flush_ci_ensure(fb_ctx.cols);
  if(!s_flush_ci)
    return;
  struct flush_cell_cache *ci = s_flush_ci;

  for(int cr = r0; cr <= r1; cr++) {
    fb_cell_t *row = &fb_ctx.cells[(size_t)cr * (size_t)fb_ctx.cols];
    u32        py0 = (u32)fb_ctx.margin_y + (u32)cr * (u32)fb_ctx.cell_h;

    for(int cc = 0; cc < fb_ctx.cols; cc++) {
      const fb_cell_t *c = &row[cc];
      ci[cc].active      = (c->dirty != 0);
      if(!ci[cc].active)
        continue;

      u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
      u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

      ci[cc].bg_pk = BGRA_OPAQUE_ALPHA | eff_bg;
      ci[cc].fg_pk =
          BGRA_OPAQUE_ALPHA | eff_fg; /* always set — underline needs it */
      ci[cc].underline = (c->attr & FB_ATTR_UNDERLINE) != 0;

      bool bg_only =
          (!fb_ctx.atlas_active || c->cp == ' ' ||
           ((c->attr & FB_ATTR_BLINK) && !fb_ctx.cell_blink_on));
      if(!bg_only) {
        u32 idx = atlas_lookup_attr(c->cp, c->attr);
        if(idx == ATLAS_NO_GLYPH || idx >= fb_ctx.atlas_n_glyphs) {
          bg_only = true;
        } else {
          ci[cc].fg_pk = BGRA_OPAQUE_ALPHA | eff_fg;
          ci[cc].fg_r  = (eff_fg >> 16) & BYTE_MASK;
          ci[cc].fg_g  = (eff_fg >> 8) & BYTE_MASK;
          ci[cc].fg_b  = eff_fg & BYTE_MASK;
          ci[cc].bg_r  = (eff_bg >> 16) & BYTE_MASK;
          ci[cc].bg_g  = (eff_bg >> 8) & BYTE_MASK;
          ci[cc].bg_b  = eff_bg & BYTE_MASK;
          ci[cc].glyph_base =
              fb_ctx.atlas_pixels + (size_t)idx * (size_t)fb_ctx.atlas_cell_h *
                                        (size_t)fb_ctx.atlas_stride;
        }
      }
      if(bg_only)
        ci[cc].glyph_base = NULL;
    }

    for(u32 spy = 0; spy < (u32)fb_ctx.cell_h; spy++) {
      volatile u32 *fb_line =
          (volatile u32 *)(fb_ctx.base + (u64)(py0 + spy) * fb_ctx.pitch +
                           (u64)fb_ctx.margin_x * FB_BYTES_PER_PIXEL_32);
      for(int cc = 0; cc < fb_ctx.cols; cc++) {
        if(!ci[cc].active)
          continue;
        volatile u32 *dst = fb_line + (size_t)cc * (size_t)fb_ctx.cell_w;

        if(ci[cc].underline &&
           spy >= (u32)fb_ctx.cell_h - UNDERLINE_THICKNESS_PX) {
          fill32(dst, ci[cc].fg_pk, (u32)fb_ctx.cell_w);
          continue;
        }
        if(!ci[cc].glyph_base || spy >= fb_ctx.atlas_cell_h) {
          fill32(dst, ci[cc].bg_pk, (u32)fb_ctx.cell_w);
          continue;
        }

        const u8 *src =
            ci[cc].glyph_base + (size_t)spy * (size_t)fb_ctx.atlas_stride;
        blend_glyph_row(
            dst, src, acw, atlas_bypp, ci[cc].fg_r, ci[cc].fg_g, ci[cc].fg_b,
            ci[cc].bg_r, ci[cc].bg_g, ci[cc].bg_b, ci[cc].fg_pk, ci[cc].bg_pk
        );
      }
    }
    for(int cc = 0; cc < fb_ctx.cols; cc++)
      row[cc].dirty = 0;
  }
}

/**
 * @brief Emit one codepoint at the cell cursor and advance, wrapping or
 * scrolling as needed.
 *
 * Identical-content writes are skipped so line editors that repaint
 * unchanged regions every keystroke don't hammer VRAM. In batch mode the
 * write only marks the cell dirty + grows the row range; outside batch it
 * blits immediately so a one-off @c write(2) is visible without an explicit
 * flush.
 *
 * @param cp  Unicode codepoint.
 */
void put_cp_at_cursor(u32 cp)
{
  if(fb_ctx.cx >= fb_ctx.cols) {
    fb_ctx.cx = 0;
    fb_ctx.cy++;
    if(fb_ctx.cy >= fb_ctx.rows) {
      scroll_one();
      fb_ctx.cy = fb_ctx.rows - 1;
    }
  }
  fb_cell_t *c =
      &fb_ctx
           .cells[(size_t)fb_ctx.cy * (size_t)fb_ctx.cols + (size_t)fb_ctx.cx];
  /* Skip when nothing actually changed — avoids redundant VRAM writes during
   * line-editor redraws that repaint identical content. */
  if(c->cp != cp || c->fg != fb_ctx.cur_fg || c->bg != fb_ctx.cur_bg ||
     c->attr != fb_ctx.cur_attr) {
    c->cp   = cp;
    c->fg   = fb_ctx.cur_fg;
    c->bg   = fb_ctx.cur_bg;
    c->attr = fb_ctx.cur_attr;
    if(fb_ctx.in_batch) {
      c->dirty = 1;
      if(fb_ctx.cy < fb_ctx.batch_r0)
        fb_ctx.batch_r0 = fb_ctx.cy;
      if(fb_ctx.cy > fb_ctx.batch_r1)
        fb_ctx.batch_r1 = fb_ctx.cy;
    } else {
      blit_cell(fb_ctx.cx, fb_ctx.cy);
    }
  }
  fb_ctx.last_cp = cp;
  fb_ctx.cx++;
}
