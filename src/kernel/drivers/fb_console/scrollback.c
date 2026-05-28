/**
 * @file src/kernel/drivers/fb_console/scrollback.c
 * @brief Scrollback ring + deferred pixel-scroll for the framebuffer console.
 *
 * Two responsibilities, kept together because they share a state machine:
 *   - A ring buffer of recent rows that the keyboard layer can scroll into
 *     view (Shift-PgUp/PgDn). Lines flow into the ring as rows scroll off
 *     the top of the live grid.
 *   - A counter of "rows that need to be scrolled out at the next flush"
 *     so a write that emits N newlines collapses to one VRAM blit instead
 *     of N. Pixel writes are MMIO and bandwidth-bound; this is the single
 *     biggest win for `ls`-style bursts.
 */

#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Rows whose pixels still need to be scrolled out at the next flush.
 *
 * Updated by @ref scroll_one during a write and consumed by
 * @ref flush_pending_scroll at end-of-write. Deferring collapses a write that
 * emits N newlines into a single VRAM blit instead of N — the biggest win for
 * @c ls -style bursts where MMIO bandwidth dominates.
 */
static int s_pending_scroll = 0;

/**
 * @brief Scrollback ring storage: @c SCROLLBACK_ROWS rows of @ref s_sb_cols
 * cells each, kmalloc'd on first need.
 *
 * NULL until the first @ref scrollback_alloc_for; reflowing to a different
 * column count discards the buffer entirely because cell coordinates have no
 * defined meaning across a column reshape.
 */
static fb_cell_t *s_sb_buf = NULL;

/**
 * @brief Column count captured when @ref s_sb_buf was allocated.
 *
 * Tracked separately so the write paths can detect a stale buffer (after a
 * reflow without a re-alloc) and skip the push instead of striding into
 * mis-shaped storage.
 */
static int s_sb_cols = 0;

/**
 * @brief Ring head index in rows — slot of the oldest stored row.
 *
 * Wraps via modulo @c SCROLLBACK_ROWS; combined with @ref s_sb_used to derive
 * the write slot when the ring is not yet full.
 */
static int s_sb_head = 0;

/**
 * @brief Rows currently stored in the ring. Saturates at @c SCROLLBACK_ROWS;
 * past that the ring overwrites the oldest row and @ref s_sb_head advances.
 */
static int s_sb_used = 0;

/**
 * @brief User-visible scrollback offset in rows from the live grid bottom.
 *
 * Zero means "show the live grid". Positive values shift the visible window
 * up into history; @ref scrollback_repaint clamps to @ref s_sb_used so PgUp
 * past the start of history is a no-op.
 */
static int s_sb_view = 0;

/**
 * @brief Copy the live top row into the scrollback ring before it gets
 * shifted off.
 *
 * Skips when the ring is missing or stale (cols mismatch); the live grid
 * keeps scrolling, but PgUp won't reach this row. Wraps via modulo at the
 * ring boundary so the oldest row gets overwritten when capacity is hit.
 */
static void scrollback_push_top_row(void)
{
  if(!s_sb_buf || s_sb_cols != fb_ctx.cols)
    return;
  int slot;
  if(s_sb_used < SCROLLBACK_ROWS) {
    slot = (s_sb_head + s_sb_used) % SCROLLBACK_ROWS;
    s_sb_used++;
  } else {
    slot      = s_sb_head;
    s_sb_head = (s_sb_head + 1) % SCROLLBACK_ROWS;
  }
  kmemcpy(
      &s_sb_buf[(size_t)slot * (size_t)fb_ctx.cols], &fb_ctx.cells[0],
      (size_t)fb_ctx.cols * sizeof(fb_cell_t)
  );
}

/**
 * @brief Shift every grid row up by one and blank the freshly exposed bottom.
 *
 * Pure cell-grid bookkeeping; the pixel side is deferred via
 * @ref s_pending_scroll so the renderer can collapse N consecutive scrolls
 * into one VRAM kmemcpy.
 */
static void scrollback_shift_grid_up(void)
{
  size_t row_bytes = (size_t)fb_ctx.cols * sizeof(fb_cell_t);
  for(int r = 0; r < fb_ctx.rows - 1; r++)
    kmemcpy(
        &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols],
        &fb_ctx.cells[(size_t)(r + 1) * (size_t)fb_ctx.cols], row_bytes
    );
  for(int c = 0; c < fb_ctx.cols; c++) {
    fb_cell_t *cell =
        &fb_ctx.cells
             [(size_t)(fb_ctx.rows - 1) * (size_t)fb_ctx.cols + (size_t)c];
    cell->cp    = (u32)' ';
    cell->fg    = fb_ctx.cur_fg;
    cell->bg    = fb_ctx.cur_bg;
    cell->attr  = 0;
    cell->dirty = 0;
  }
}

/**
 * @brief Drop the top row off the grid, shift the rest up, blank the bottom,
 * and queue one pixel-move for flush time.
 *
 * Pushes the scrolled-off row into the ring so it stays accessible via
 * Shift-PgUp. The pixel side is deferred to @ref flush_pending_scroll so a
 * write that emits N newlines collapses to one VRAM blit instead of N — by
 * far the biggest win for @c ls -style bursts where MMIO bandwidth dominates.
 *
 * Expands the in-batch dirty range to the full grid because cells have
 * shifted rows; without that, @ref flush_batch would miss the moved cells.
 */
void scroll_one(void)
{
  scrollback_push_top_row();
  scrollback_shift_grid_up();
  s_pending_scroll++;
  caret_clear_drawn();
  if(fb_ctx.in_batch) {
    fb_ctx.batch_r0 = 0;
    fb_ctx.batch_r1 = fb_ctx.rows - 1;
  }
}

/**
 * @brief Repaint the visible grid blending scrollback history with live cells.
 *
 * Rows above @c s_sb_view come from the ring; the rest come from @c fb_ctx
 * shifted by the offset. Routed through @ref blit_cell_data (not blit_cell)
 * because the ring's cells are private — they must not get written back into
 * @c fb_ctx.cells, only displayed. Caret is erased first so the inverted
 * block doesn't end up duplicated when the live view comes back.
 */
void scrollback_repaint(void)
{
  if(!fb_ctx.base || !fb_ctx.cells)
    return;
  caret_erase();
  for(int r = 0; r < fb_ctx.rows; r++) {
    const fb_cell_t *src;
    fb_cell_t        blank;
    if(r < s_sb_view) {
      int sb_idx = s_sb_used - s_sb_view + r;
      if(sb_idx < 0 || !s_sb_buf) {
        kmemset(&blank, 0, sizeof blank);
        blank.cp = ' ';
        blank.fg = fb_ctx.default_fg;
        blank.bg = fb_ctx.default_bg;
        src      = &blank;
      } else {
        int slot = (s_sb_head + sb_idx) % SCROLLBACK_ROWS;
        src      = &s_sb_buf[(size_t)slot * (size_t)fb_ctx.cols];
      }
      for(int c = 0; c < fb_ctx.cols; c++)
        blit_cell_data(src + c, c, r);
    } else {
      src = &fb_ctx.cells[(size_t)(r - s_sb_view) * (size_t)fb_ctx.cols];
      for(int c = 0; c < fb_ctx.cols; c++)
        blit_cell_data(src + c, c, r);
    }
  }
}

/**
 * @brief Snap back to the live view (offset = 0) and force a full repaint.
 *
 * Marks every live cell dirty so the next @ref flush_batch picks them up —
 * pixels currently show scrollback rows, so a cheaper "draw the cells under
 * the previous view" would leave the rest unchanged. No-op when already live.
 */
void scrollback_exit(void)
{
  if(s_sb_view == 0)
    return;
  s_sb_view = 0;
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c].dirty = 1;
}

/**
 * @brief Scroll the view @p lines rows back into history.
 *
 * Public API called by the keyboard layer on Shift-PgUp. Clamps to the
 * number of rows actually stored so a power-user holding the key down
 * doesn't run past the ring.
 *
 * @param lines  Positive number of rows to reveal from history.
 */
void fb_console_scrollback_up(int lines)
{
  if(!s_sb_buf || s_sb_used == 0 || lines <= 0)
    return;
  s_sb_view += lines;
  if(s_sb_view > s_sb_used)
    s_sb_view = s_sb_used;
  scrollback_repaint();
}

/**
 * @brief Scroll the view @p lines rows toward the live tail.
 *
 * Symmetric counterpart to @ref fb_console_scrollback_up. Reaching offset 0
 * routes through @ref scrollback_exit to mark everything dirty for a clean
 * resume; intermediate positions just repaint the blended view.
 *
 * @param lines  Positive number of rows to advance toward live.
 */
void fb_console_scrollback_down(int lines)
{
  if(s_sb_view == 0 || lines <= 0)
    return;
  s_sb_view -= lines;
  if(s_sb_view < 0)
    s_sb_view = 0;
  if(s_sb_view == 0)
    scrollback_exit();
  else
    scrollback_repaint();
}

/**
 * @brief Drain the pending-scroll counter in one VRAM copy plus dirty marks.
 *
 * 32-bpp fast path does a single in-place kmemcpy of the scrolled-up region
 * (saves N MMIO write passes). Other depths fall back to "mark every cell
 * dirty" and let @ref flush_batch re-blit them. Mouse cursor is invalidated
 * first so the kmemcpy doesn't drag a stale arrow to a new row.
 */
/**
 * @brief 32-bpp fast path: kmemcpy the surviving rows up by @p n cells, then
 *        mark the freshly exposed bottom rows dirty for the next flush.
 *
 * One MMIO copy beats N per-row blits when bandwidth is the bottleneck —
 * that's the whole reason scrolling is deferred.
 *
 * @param n  Number of cell-rows to scroll out (clamped to @c fb_ctx.rows).
 */
static void flush_scroll_32bpp(int n)
{
  u32 scroll_px = (u32)n * (u32)fb_ctx.cell_h;
  u32 total_px  = (u32)fb_ctx.rows * (u32)fb_ctx.cell_h;
  u32 copy_px   = total_px - scroll_px;
  if(copy_px > 0) {
    u8       *dst = (u8 *)fb_ctx.base + (u64)fb_ctx.margin_y * fb_ctx.pitch;
    const u8 *src = dst + (u64)scroll_px * fb_ctx.pitch;
    kmemcpy(dst, src, (u64)copy_px * fb_ctx.pitch);
  }
  int first_new = fb_ctx.rows - n;
  for(int r = first_new; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c].dirty = 1;
  if(fb_ctx.batch_r0 > first_new)
    fb_ctx.batch_r0 = first_new;
  if(fb_ctx.batch_r1 < fb_ctx.rows - 1)
    fb_ctx.batch_r1 = fb_ctx.rows - 1;
}

/**
 * @brief Slow-path scroll for non-32-bpp framebuffers: mark every cell dirty
 *        and let @ref flush_batch re-blit them.
 *
 * No MMIO kmemcpy here because the row stride math depends on bytes-per-pixel
 * the slow path can't assume; full repaint is simpler and 24/16-bpp is not
 * shipped today.
 */
static void flush_scroll_slow(void)
{
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c].dirty = 1;
  fb_ctx.batch_r0 = 0;
  fb_ctx.batch_r1 = fb_ctx.rows - 1;
}

/**
 * @brief Drain the pending-scroll counter in one VRAM copy plus dirty marks.
 *
 * 32-bpp fast path does a single in-place kmemcpy of the scrolled-up region
 * (saves N MMIO write passes). Other depths fall back to "mark every cell
 * dirty" and let @ref flush_batch re-blit them. Mouse cursor is invalidated
 * first so the kmemcpy doesn't drag a stale arrow to a new row.
 */
void flush_pending_scroll(void)
{
  if(s_pending_scroll <= 0)
    return;
  int n            = s_pending_scroll;
  s_pending_scroll = 0;
  if(n > fb_ctx.rows)
    n = fb_ctx.rows;

  mouse_cursor_invalidate_for_scroll();
  if(fb_ctx.base && fb_ctx.bytes_pp == FB_BYTES_PER_PIXEL_32)
    flush_scroll_32bpp(n);
  else
    flush_scroll_slow();
}

/**
 * @brief (Re)allocate the scrollback ring for @p cols columns.
 *
 * Existing contents are dropped because cell coordinates do not survive a
 * column-count change in any well-defined way. Called on initial bring-up
 * and on every @ref fb_console_set_atlas reflow.
 *
 * @param cols  New column count.
 */
void scrollback_alloc_for(int cols)
{
  if(s_sb_buf)
    kfree(s_sb_buf);
  s_sb_buf =
      kmalloc((size_t)SCROLLBACK_ROWS * (size_t)cols * sizeof(fb_cell_t));
  s_sb_cols = cols;
  s_sb_head = s_sb_used = s_sb_view = 0;
}

/**
 * @brief Forget pending scrolls without flushing them.
 *
 * For paths that are about to repaint the whole grid anyway (reclaim,
 * set_atlas) — flushing first would just waste a VRAM copy.
 */
void scrollback_drop_pending(void)
{
  s_pending_scroll = 0;
}
