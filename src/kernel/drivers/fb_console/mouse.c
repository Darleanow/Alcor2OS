/**
 * @file src/kernel/drivers/fb_console/mouse.c
 * @brief Software mouse cursor overlay for the framebuffer console.
 *
 * 12×19 white arrow with a one-pixel black halo so the pointer stays
 * legible on any background. Erase is destructive: it paints the framebuffer
 * default-bg over the cursor footprint and re-blits the cells underneath,
 * which is correct as long as the underlying cells are still valid (the
 * scroll path calls @ref mouse_cursor_invalidate_for_scroll before moving
 * pixels so a stale copy never gets dragged along).
 */

#include <alcor2/drivers/mouse.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Opaque arrow cursor bitmap, @ref MOUSE_CURSOR_W wide x
 * @ref MOUSE_CURSOR_H tall.
 *
 * Each @c u16 is one row of the cursor; bit @c (BIT_MSB_16 @c >> @c col) is
 * set when that pixel is part of the white body. The halo pass in
 * @ref mouse_cursor_paint walks one cell past every edge so an unset cell
 * with a set neighbour gets painted black first, then the body pass overpaints
 * the set cells white. Two-pass to keep both inner loops branch-light.
 */
static const u16 cursor_bitmap[19] = {
    0x8000, /* 1............... */
    0xC000, /* 11.............. */
    0xE000, /* 111............. */
    0xF000, /* 1111............ */
    0xF800, /* 11111........... */
    0xFC00, /* 111111.......... */
    0xFE00, /* 1111111......... */
    0xFF00, /* 11111111........ */
    0xFF80, /* 111111111....... */
    0xFFC0, /* 1111111111...... */
    0xFFE0, /* 11111111111..... */
    0xFE00, /* 1111111......... */
    0xEE00, /* 111.111......... */
    0xCE00, /* 11..111......... */
    0x8700, /* 1....111........ */
    0x0700, /* .....111........ */
    0x0380, /* ......111....... */
    0x0380, /* ......111....... */
    0x0100, /* .......1........ */
};

/** @brief Width of the @ref cursor_bitmap in pixels. */
#define MOUSE_CURSOR_W 12

/** @brief Height of the @ref cursor_bitmap in pixels. */
#define MOUSE_CURSOR_H 19

/** @brief Fill (white) colour for the cursor body. */
#define MOUSE_CURSOR_FILL 0xFFFFFFu

/** @brief Halo (black) colour painted around the body for contrast. */
#define MOUSE_CURSOR_HALO 0x000000u

/** @brief MSB of a 16-bit bitmap row — used as the seed for the left-to-right
 * column scan via @c (BIT_MSB_16 >> col). Named so the shift in @ref
 * cursor_bit reads as a sweep, not a magic constant. */
#define BIT_MSB_16 0x8000u

/**
 * @brief Position and paint-state of the software mouse cursor.
 *
 * @c drawn defaults to @c false so a fresh boot does not show a stray cursor
 * at (0,0) before the user moves the mouse for the first time. Stored
 * separately from the PS/2 driver's position because the renderer also needs
 * "where was the cursor last painted" to erase before repainting.
 */
struct mouse_cur_state
{
  bool drawn; /**< true when the arrow is currently on the framebuffer. */
  i32  x;     /**< Last painted x in framebuffer pixels. */
  i32  y;     /**< Last painted y in framebuffer pixels. */
};

static struct mouse_cur_state mouse_cur;

/**
 * @brief Test whether the (row, col) of the arrow bitmap is set.
 *
 * Out-of-range coordinates return false so the halo loop can probe one cell
 * past every edge without bounds-checking the bitmap itself — that one
 * negative result is what gives the cursor its one-pixel outline.
 *
 * @param row  Row index (may be negative for halo probing).
 * @param col  Column index (may be negative for halo probing).
 * @return @c true when the bitmap bit is set, @c false otherwise.
 */
static inline bool cursor_bit(int row, int col)
{
  if(row < 0 || row >= MOUSE_CURSOR_H || col < 0 || col >= MOUSE_CURSOR_W)
    return false;
  return (cursor_bitmap[row] & (BIT_MSB_16 >> col)) != 0;
}

/**
 * @brief Paint the arrow at (@p cx, @p cy) in two passes.
 *
 * The halo pass paints each unset cell that has at least one set neighbour,
 * giving a one-pixel black outline; the fill pass paints the set cells white.
 * Doing it as two passes (rather than one with branch-per-pixel) keeps both
 * loops branch-light and lets the inner halo loop bail early once a single
 * neighbour is found.
 *
 * @param cx  Cursor x in framebuffer coordinates (top-left of the arrow).
 * @param cy  Cursor y in framebuffer coordinates.
 */
static void mouse_cursor_paint(i32 cx, i32 cy)
{
  for(int row = -1; row <= MOUSE_CURSOR_H; row++) {
    for(int col = -1; col <= MOUSE_CURSOR_W; col++) {
      if(cursor_bit(row, col))
        continue;
      bool border = false;
      for(int dy = -1; dy <= 1 && !border; dy++)
        for(int dx = -1; dx <= 1 && !border; dx++)
          if((dx || dy) && cursor_bit(row + dy, col + dx))
            border = true;
      if(border)
        fb_put_pixel((u32)(cx + col), (u32)(cy + row), MOUSE_CURSOR_HALO);
    }
  }
  for(int row = 0; row < MOUSE_CURSOR_H; row++) {
    for(int col = 0; col < MOUSE_CURSOR_W; col++) {
      if(cursor_bit(row, col))
        fb_put_pixel((u32)(cx + col), (u32)(cy + row), MOUSE_CURSOR_FILL);
    }
  }
}

/**
 * @brief Wipe the arrow's footprint and restore the cells beneath.
 *
 * bg-fills first so margin pixels (outside the cell grid) get cleaned —
 * @ref blit_cell only repaints whole cells, so an arrow that overhangs the
 * margin would otherwise leave a residual rectangle there.
 *
 * @param cx  Last-painted cursor x.
 * @param cy  Last-painted cursor y.
 */
static void mouse_cursor_erase(i32 cx, i32 cy)
{
  int x0 = cx - 1;
  int y0 = cy - 1;
  int x1 = cx + MOUSE_CURSOR_W;
  int y1 = cy + MOUSE_CURSOR_H;

  if(x0 < 0)
    x0 = 0;
  if(y0 < 0)
    y0 = 0;
  if(x1 >= (int)fb_ctx.width)
    x1 = (int)fb_ctx.width - 1;
  if(y1 >= (int)fb_ctx.height)
    y1 = (int)fb_ctx.height - 1;
  for(int y = y0; y <= y1; y++)
    for(int x = x0; x <= x1; x++)
      fb_put_pixel((u32)x, (u32)y, fb_ctx.default_bg);

  int cw = fb_ctx.cell_w ? fb_ctx.cell_w : 1;
  int ch = fb_ctx.cell_h ? fb_ctx.cell_h : 1;
  int c0 = (x0 - fb_ctx.margin_x) / cw;
  int c1 = (x1 - fb_ctx.margin_x) / cw;
  int r0 = (y0 - fb_ctx.margin_y) / ch;
  int r1 = (y1 - fb_ctx.margin_y) / ch;
  if(c0 < 0)
    c0 = 0;
  if(r0 < 0)
    r0 = 0;
  if(c1 >= fb_ctx.cols)
    c1 = fb_ctx.cols - 1;
  if(r1 >= fb_ctx.rows)
    r1 = fb_ctx.rows - 1;
  for(int r = r0; r <= r1; r++)
    for(int c = c0; c <= c1; c++)
      blit_cell(c, r);
}

/**
 * @brief Paint or move the software mouse cursor to the current PS/2 position.
 *
 * Early-outs in two stages so the PIT tick path stays cheap at rest: first
 * before any work when the user has never moved the mouse (avoids a stray
 * cursor at boot), then after fetching the position when it matches the
 * already-painted location. Erases both the old AND new positions before
 * painting so a fast move past a scroll race can't leave a stale halo.
 */
void mouse_cursor_render(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  if(!mouse_has_moved())
    return;
  i32 nx, ny;
  mouse_get_cursor(&nx, &ny);

  if(mouse_cur.drawn && nx == mouse_cur.x && ny == mouse_cur.y)
    return;

  if(mouse_cur.drawn)
    mouse_cursor_erase(mouse_cur.x, mouse_cur.y);
  mouse_cursor_erase(nx, ny);
  mouse_cursor_paint(nx, ny);
  mouse_cur.drawn = true;
  mouse_cur.x     = nx;
  mouse_cur.y     = ny;
}

/**
 * @brief Erase the cursor (if drawn) so an imminent scrollback pixel-move
 * doesn't kmemcpy a stale arrow into a new row.
 *
 * Called by @ref flush_pending_scroll before its scroll-region kmemcpy. The
 * @c drawn flag is cleared so the next @ref mouse_cursor_render does a fresh
 * paint rather than skipping as "already where it should be".
 */
void mouse_cursor_invalidate_for_scroll(void)
{
  if(!mouse_cur.drawn)
    return;
  mouse_cursor_erase(mouse_cur.x, mouse_cur.y);
  mouse_cur.drawn = false;
}

/**
 * @brief Forget the painted-at position without touching pixels.
 *
 * For paths that are about to overwrite the framebuffer themselves
 * (yield, reclaim, blink-driven full re-blit): erasing first would just be
 * extra work, but the next @ref mouse_cursor_render still needs to redraw
 * rather than skip-as-unchanged.
 */
void mouse_cursor_drop(void)
{
  mouse_cur.drawn = false;
}
