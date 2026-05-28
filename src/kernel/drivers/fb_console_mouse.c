/**
 * @file src/kernel/drivers/fb_console_mouse.c
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
#include <kernel/drivers/fb_console_internal.h>

/* Opaque arrow cursor, 12 wide × 19 tall. Painted as a filled white shape
 * with an automatic 1-pixel black halo: any pixel adjacent to a "1" bit gets
 * black first, then the "1" pixels themselves are overpainted white. Visible
 * on any background. */
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

/* "drawn" defaults to false so a fresh boot does not show a stray cursor at
 * (0,0) until the user moves the mouse for the first time. */
static struct
{
  bool drawn;
  i32  x;
  i32  y;
} mouse_cur;

static inline bool cursor_bit(int row, int col)
{
  if(row < 0 || row >= MOUSE_CURSOR_H || col < 0 || col >= MOUSE_CURSOR_W)
    return false;
  return (cursor_bitmap[row] & (0x8000u >> col)) != 0;
}

static void mouse_cursor_paint(i32 cx, i32 cy)
{
  /* Halo pass: paint black at every neighbour of a "1" pixel that is not
   * itself a "1". One pixel wide outline. */
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
  /* Fill pass: white where the bitmap is set. */
  for(int row = 0; row < MOUSE_CURSOR_H; row++) {
    for(int col = 0; col < MOUSE_CURSOR_W; col++) {
      if(cursor_bit(row, col))
        fb_put_pixel((u32)(cx + col), (u32)(cy + row), MOUSE_CURSOR_FILL);
    }
  }
}

/* bg-fill first so margin pixels (outside the cell grid) get cleaned, then
 * re-blit cells to restore glyphs. */
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

void mouse_cursor_render(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  /* Keep the pointer hidden until the user actually moves it, so a fresh boot
   * doesn't show a stray cursor pinned at screen centre. */
  if(!mouse_has_moved())
    return;
  i32 nx, ny;
  mouse_get_cursor(&nx, &ny);

  /* Nothing to do if the pointer is already drawn where it belongs. Repainting
   * an unmoved cursor every tick is what makes it flicker. */
  if(mouse_cur.drawn && nx == mouse_cur.x && ny == mouse_cur.y)
    return;

  if(mouse_cur.drawn)
    mouse_cursor_erase(mouse_cur.x, mouse_cur.y);
  /* Also clear the destination, removing stale pixels from scroll races or
   * scrollback/reclaim transitions. */
  mouse_cursor_erase(nx, ny);
  mouse_cursor_paint(nx, ny);
  mouse_cur.drawn = true;
  mouse_cur.x     = nx;
  mouse_cur.y     = ny;
}

void mouse_cursor_invalidate_for_scroll(void)
{
  if(!mouse_cur.drawn)
    return;
  mouse_cursor_erase(mouse_cur.x, mouse_cur.y);
  mouse_cur.drawn = false;
}

void mouse_cursor_drop(void)
{
  mouse_cur.drawn = false;
}
