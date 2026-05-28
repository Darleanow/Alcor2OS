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

/* Rows that need to be scrolled out at the next flush. Updated by scroll_one
 * during a write; consumed by flush_pending_scroll at end-of-write. */
static int s_pending_scroll = 0;

/* Scrollback ring. Sized SCROLLBACK_ROWS * cols cells; s_sb_cols is captured
 * at allocation so the reflow path can detect a stale buffer and drop it
 * instead of writing into mis-shaped storage. */
static fb_cell_t *s_sb_buf  = NULL;
static int        s_sb_cols = 0;
static int        s_sb_head = 0;
static int        s_sb_used = 0;
static int        s_sb_view = 0;

void              scroll_one(void)
{
  size_t row_bytes = (size_t)fb_ctx.cols * sizeof(fb_cell_t);

  /* Save the row scrolling off the top into the scrollback ring. */
  if(s_sb_buf && s_sb_cols == fb_ctx.cols) {
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
        row_bytes
    );
  }

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
  s_pending_scroll++;
  caret_clear_drawn();
  /* After a scroll, dirty cells may have shifted rows — expand the range
   * to cover all rows so flush_batch() doesn't miss any. */
  if(fb_ctx.in_batch) {
    fb_ctx.batch_r0 = 0;
    fb_ctx.batch_r1 = fb_ctx.rows - 1;
  }
}

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

void scrollback_exit(void)
{
  if(s_sb_view == 0)
    return;
  s_sb_view = 0;
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c].dirty = 1;
}

void fb_console_scrollback_up(int lines)
{
  if(!s_sb_buf || s_sb_used == 0 || lines <= 0)
    return;
  s_sb_view += lines;
  if(s_sb_view > s_sb_used)
    s_sb_view = s_sb_used;
  scrollback_repaint();
}

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

void flush_pending_scroll(void)
{
  if(s_pending_scroll <= 0)
    return;

  int n            = s_pending_scroll;
  s_pending_scroll = 0;
  if(n > fb_ctx.rows)
    n = fb_ctx.rows;

  /* Drop any painted cursor so it isn't carried along as a ghost. */
  mouse_cursor_invalidate_for_scroll();

  if(fb_ctx.base && fb_ctx.bytes_pp == 4) {
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
    return;
  }

  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c].dirty = 1;
  fb_ctx.batch_r0 = 0;
  fb_ctx.batch_r1 = fb_ctx.rows - 1;
}

void scrollback_alloc_for(int cols)
{
  if(s_sb_buf)
    kfree(s_sb_buf);
  s_sb_buf = (fb_cell_t *)kmalloc(
      (size_t)SCROLLBACK_ROWS * (size_t)cols * sizeof(fb_cell_t)
  );
  s_sb_cols = cols;
  s_sb_head = s_sb_used = s_sb_view = 0;
}

void scrollback_drop_pending(void)
{
  s_pending_scroll = 0;
}
