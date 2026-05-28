/**
 * @file src/kernel/drivers/fb_console/caret.c
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
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Cell currently displaying the inverted-block cursor.
 *
 * @c -1 means "no cursor on screen right now" — @ref caret_erase short-circuits
 * on that sentinel. Tracked separately from the logical cursor (@c fb_ctx.cx /
 * @c fb_ctx.cy) so a single moved-cursor frame only re-blits the previous and
 * new cells instead of redrawing the row.
 */
static int s_drawn_x = -1;
static int s_drawn_y =
    -1; /**< @brief Companion of @ref s_drawn_x; see there. */

/**
 * @brief Re-blit the cell currently showing the inverted caret block,
 * restoring the glyph beneath. Idempotent: the @c -1 sentinel makes it a
 * no-op when nothing is painted, so callers do not need to guard themselves.
 */
void caret_erase(void)
{
  if(s_drawn_x < 0 || s_drawn_y < 0)
    return;
  if(fb_ctx.cells && s_drawn_y < fb_ctx.rows && s_drawn_x < fb_ctx.cols)
    blit_cell(s_drawn_x, s_drawn_y);
  s_drawn_x = -1;
  s_drawn_y = -1;
}

/**
 * @brief Paint the caret at @c fb_ctx.cx/cy by drawing the cell with fg/bg
 * swapped.
 *
 * Saves/restores the cell's attributes around the inverted blit so the cell
 * grid keeps the "real" glyph state — that way the next blit of the same
 * cell from any path (scroll, atlas reload, scrollback exit) does the right
 * thing without a separate "is this the caret" branch.
 */
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

/**
 * @brief @ref caret_erase followed by @ref caret_paint.
 *
 * Cheap when the cursor has not moved (erase is idempotent on the same cell
 * and paint just re-applies the inversion). Used by the PIT tick path to
 * make the caret blink — flipping @c fb_ctx.blink_on plus a refresh is the
 * whole animation.
 */
void caret_refresh(void)
{
  caret_erase();
  caret_paint();
}

/**
 * @brief Forget the tracker without repainting.
 *
 * For paths that overwrite the cell themselves (scroll, atlas reflow, fb
 * yield/reclaim): they need the caret machinery to drop its memory of the
 * old position so the next @ref caret_paint records a fresh one rather than
 * trying to erase a cell that no longer holds the inverted glyph.
 */
void caret_clear_drawn(void)
{
  s_drawn_x = -1;
  s_drawn_y = -1;
}

/**
 * @brief Mark the caret's cell dirty for the next @ref flush_batch and
 * forget the tracker.
 *
 * Used by @ref fb_console_write_begin so the batched repaint also wipes the
 * inverted block. Without this, the post-batch flush would leave the old
 * caret visible until the next blink tick — visible flicker during fast
 * output.
 */
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
