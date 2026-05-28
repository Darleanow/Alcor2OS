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

/**
 * @brief Per-cell scratch built by @ref flush_batch before walking scanlines.
 *
 * Holds everything a single cell's row blit needs so the inner scanline loop
 * never re-touches the @ref fb_cell_t — that loop already runs @c cell_h times
 * per dirty cell, so any per-cell work paid up-front is amortised.
 *
 * @c glyph_base @c == @c NULL signals "bg-only" (space, blink-off, or atlas
 * miss): the scanline loop fills the cell width with @c bg_pk and skips the
 * blend altogether.
 */
struct flush_cell_cache
{
  const u8 *glyph_base; /**< Atlas row start, or NULL for bg-only cells. */
  u32       bg_pk;      /**< 0xFF000000 | effective bg, ready for fill32. */
  u32       fg_pk; /**< 0xFF000000 | effective fg; underline uses it too. */
  u32       fg_r, fg_g, fg_b; /**< Unpacked fg channels for the blender. */
  u32       bg_r, bg_g, bg_b; /**< Unpacked bg channels for the blender. */
  bool      active;    /**< true when this cell's @c dirty bit was set. */
  bool      underline; /**< true when SGR underline is on for this cell. */
};

/**
 * @brief Per-row scratch used by @ref flush_batch.
 *
 * Sized at ~48 B per cell — at 160+ cols far too large for the 8 KiB kernel
 * stack, so heap-owned and only grown via @ref flush_ci_ensure when the grid
 * widens. NULL until the first reflow allocates it.
 */
static struct flush_cell_cache *s_flush_ci = NULL;

/**
 * @brief Capacity of @ref s_flush_ci, in cells. Tracked separately because the
 * heap allocator does not let us query a block's size.
 */
static int s_flush_ci_cols = 0;

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
 * @brief Slow-path repaint used when the framebuffer is not 32-bpp.
 *
 * Falls back to @ref blit_cell per dirty cell because the row-cache fast path
 * assumes 32-bpp fill/blend primitives. Linear in the row range — neither
 * 24/16-bpp nor the no-atlas state is performance critical, so this is fine.
 *
 * @param r0  First row in the dirty range (clamped).
 * @param r1  Last row in the dirty range (clamped).
 */
static void flush_batch_slow(int r0, int r1)
{
  for(int r = r0; r <= r1; r++)
    for(int c = 0; c < fb_ctx.cols; c++) {
      fb_cell_t *cell =
          &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c];
      if(cell->dirty) {
        cell->dirty = 0;
        blit_cell(c, r);
      }
    }
}

/**
 * @brief Populate a single @ref flush_cell_cache entry from its source cell.
 *
 * Splits the per-cell prep out of the row loop so the scratch population
 * fits in 25 LOC and is independently followable. Blink-off and atlas-miss
 * cells collapse to @c bg_only by clearing @c glyph_base.
 *
 * @param dst  Scratch entry to fill (caller indexes per column).
 * @param c    Source cell.
 */
static void flush_prep_cell(struct flush_cell_cache *dst, const fb_cell_t *c)
{
  dst->active = (c->dirty != 0);
  if(!dst->active)
    return;

  u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
  u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

  dst->bg_pk      = BGRA_OPAQUE_ALPHA | eff_bg;
  dst->fg_pk      = BGRA_OPAQUE_ALPHA | eff_fg;
  dst->underline  = (c->attr & FB_ATTR_UNDERLINE) != 0;
  dst->glyph_base = NULL;

  bool bg_only =
      (!fb_ctx.atlas_active || c->cp == ' ' ||
       ((c->attr & FB_ATTR_BLINK) && !fb_ctx.cell_blink_on));
  if(bg_only)
    return;

  u32 idx = atlas_lookup_attr(c->cp, c->attr);
  if(idx == ATLAS_NO_GLYPH || idx >= fb_ctx.atlas_n_glyphs)
    return;

  dst->fg_r       = (eff_fg >> BGRA_RED_SHIFT) & BYTE_MASK;
  dst->fg_g       = (eff_fg >> BGRA_GREEN_SHIFT) & BYTE_MASK;
  dst->fg_b       = eff_fg & BYTE_MASK;
  dst->bg_r       = (eff_bg >> BGRA_RED_SHIFT) & BYTE_MASK;
  dst->bg_g       = (eff_bg >> BGRA_GREEN_SHIFT) & BYTE_MASK;
  dst->bg_b       = eff_bg & BYTE_MASK;
  dst->glyph_base = fb_ctx.atlas_pixels + (size_t)idx *
                                              (size_t)fb_ctx.atlas_cell_h *
                                              (size_t)fb_ctx.atlas_stride;
}

/**
 * @brief Paint one scanline of one dirty cell using its prepared scratch.
 *
 * Three sub-cases, in order: underline bar overlay, bg-only fill, glyph-row
 * blend. The early returns keep the inner loop branch-light.
 *
 * @param ci_cc       Scratch entry for this column.
 * @param dst         Destination row pointer at this cell's left edge.
 * @param spy         Scanline offset within the cell (0..cell_h-1).
 * @param atlas_bypp  Bytes per atlas pixel.
 * @param acw         Effective glyph width.
 */
static void flush_paint_scanline(
    const struct flush_cell_cache *ci_cc, volatile u32 *dst, u32 spy,
    u32 atlas_bypp, u32 acw
)
{
  if(ci_cc->underline && spy >= (u32)fb_ctx.cell_h - UNDERLINE_THICKNESS_PX) {
    fill32(dst, ci_cc->fg_pk, (u32)fb_ctx.cell_w);
    return;
  }
  if(!ci_cc->glyph_base || spy >= fb_ctx.atlas_cell_h) {
    fill32(dst, ci_cc->bg_pk, (u32)fb_ctx.cell_w);
    return;
  }
  const u8 *src = ci_cc->glyph_base + (size_t)spy * (size_t)fb_ctx.atlas_stride;
  blend_glyph_row(
      dst, src, acw, atlas_bypp, ci_cc->fg_r, ci_cc->fg_g, ci_cc->fg_b,
      ci_cc->bg_r, ci_cc->bg_g, ci_cc->bg_b, ci_cc->fg_pk, ci_cc->bg_pk
  );
}

/**
 * @brief Walk the scratch across one cell-row's scanlines and clear the
 * dirty bits when done.
 *
 * Iterates scanlines outermost, columns innermost — same cache walk as the
 * monolithic loop, so the per-cell scratch payload stays hot in L1 across the
 * @c cell_h iterations.
 *
 * @param ci          Per-column scratch, prepped by @ref flush_prep_cell.
 * @param row         First cell of the row in @c fb_ctx.cells.
 * @param py0         Pixel-y of the row's top edge.
 * @param atlas_bypp  Bytes per atlas pixel.
 * @param acw         Effective glyph width.
 */
static void flush_paint_row(
    const struct flush_cell_cache *ci, fb_cell_t *row, u32 py0, u32 atlas_bypp,
    u32 acw
)
{
  for(u32 spy = 0; spy < (u32)fb_ctx.cell_h; spy++) {
    volatile u32 *fb_line =
        (volatile u32 *)(fb_ctx.base + (u64)(py0 + spy) * fb_ctx.pitch +
                         (u64)fb_ctx.margin_x * FB_BYTES_PER_PIXEL_32);
    for(int cc = 0; cc < fb_ctx.cols; cc++) {
      if(!ci[cc].active)
        continue;
      flush_paint_scanline(
          &ci[cc], fb_line + (size_t)cc * (size_t)fb_ctx.cell_w, spy,
          atlas_bypp, acw
      );
    }
  }
  for(int cc = 0; cc < fb_ctx.cols; cc++)
    row[cc].dirty = 0;
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
    flush_batch_slow(r0, r1);
    return;
  }

  if(!s_flush_ci || s_flush_ci_cols < fb_ctx.cols)
    flush_ci_ensure(fb_ctx.cols);
  if(!s_flush_ci)
    return;

  u32 atlas_bypp = (fb_ctx.atlas_bpp + BITS_PER_BYTE - 1u) / BITS_PER_BYTE;
  u32 acw = (fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w) ? fb_ctx.atlas_cell_w
                                                       : (u32)fb_ctx.cell_w;

  for(int cr = r0; cr <= r1; cr++) {
    fb_cell_t *row = &fb_ctx.cells[(size_t)cr * (size_t)fb_ctx.cols];
    u32        py0 = (u32)fb_ctx.margin_y + (u32)cr * (u32)fb_ctx.cell_h;
    for(int cc = 0; cc < fb_ctx.cols; cc++)
      flush_prep_cell(&s_flush_ci[cc], &row[cc]);
    flush_paint_row(s_flush_ci, row, py0, atlas_bypp, acw);
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
