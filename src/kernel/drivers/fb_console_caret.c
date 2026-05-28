/**
 * @file src/kernel/drivers/fb_console_caret.c
 * @brief Text caret (blinking block cursor) tracker for the framebuffer
 * console.
 *
 * Named @c caret_* (not @c cursor_*) to keep it unambiguously distinct from
 * the software mouse cursor that lives in @ref fb_console_mouse.c. Owns the
 * "last drawn at (x, y)" tracker so its statics never leak out via headers —
 * the few cross-file consumers that need to invalidate or query it go through
 * the exported helpers below.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console_internal.h>

/* Position of the cell currently displaying the inverted-block cursor.
 * @c -1 means "no cursor on screen right now"; @ref caret_erase treats it
 * as a no-op. Tracked so a moved cursor can wipe its old block by re-blitting
 * exactly one cell instead of repainting the row. */
static int s_drawn_x = -1;
static int s_drawn_y = -1;

void       caret_erase(void)
{
  if(s_drawn_x < 0 || s_drawn_y < 0)
    return;
  if(fb_ctx.cells && s_drawn_y < fb_ctx.rows && s_drawn_x < fb_ctx.cols)
    blit_cell(s_drawn_x, s_drawn_y);
  s_drawn_x = -1;
  s_drawn_y = -1;
}

void caret_paint(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  if(!fb_ctx.cursor_visible || !fb_ctx.blink_on)
    return;
  int x = fb_ctx.cx;
  int y = fb_ctx.cy;
  if(x >= fb_ctx.cols)
    x = fb_ctx.cols - 1;
  if(y >= fb_ctx.rows)
    y = fb_ctx.rows - 1;
  if(x < 0 || y < 0)
    return;

  fb_cell_t *cell = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols + (size_t)x];
  u32        saved_fg = cell->fg;
  u32        saved_bg = cell->bg;
  u16        saved_at = cell->attr;

  cell->fg   = saved_bg;
  cell->bg   = saved_fg;
  cell->attr = 0;
  blit_cell(x, y);
  cell->fg   = saved_fg;
  cell->bg   = saved_bg;
  cell->attr = saved_at;

  s_drawn_x = x;
  s_drawn_y = y;
}

void caret_refresh(void)
{
  caret_erase();
  caret_paint();
}

void caret_clear_drawn(void)
{
  s_drawn_x = -1;
  s_drawn_y = -1;
}

void caret_invalidate_in_batch(void)
{
  if(s_drawn_x < 0 || s_drawn_y < 0)
    return;
  fb_cell_t *cc =
      &fb_ctx
           .cells[(size_t)s_drawn_y * (size_t)fb_ctx.cols + (size_t)s_drawn_x];
  cc->dirty = 1;
  if(s_drawn_y < fb_ctx.batch_r0)
    fb_ctx.batch_r0 = s_drawn_y;
  if(s_drawn_y > fb_ctx.batch_r1)
    fb_ctx.batch_r1 = s_drawn_y;
  s_drawn_x = -1;
  s_drawn_y = -1;
}
