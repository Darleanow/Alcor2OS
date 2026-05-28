/**
 * @file src/kernel/drivers/fb_console/api.c
 * @brief Runtime framebuffer text console with UTF-8 and ANSI/CSI support.
 *
 * Maintains an in-RAM cell grid, blits glyphs via the compiled-in CP437 bitmap
 * or a userspace-supplied Fira atlas, and renders scrollback history.
 * Takes over the framebuffer from the early boot logger once kmalloc is up.
 */

#include <alcor2/drivers/fb_console.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/types.h>
#include <drivers/console/font.h>
#include <kernel/drivers/fb_console/internal.h>

/* Forward declaration — avoids pulling in proc/signal.h for one call. */
#define SIGWINCH 28
void             proc_signal_broadcast(int signum);

fb_console_ctx_t fb_ctx;

bool fb_console_init(void *fb, u64 width, u64 height, u64 pitch, u16 bpp)
{
  fb_ctx.base       = (volatile u8 *)fb;
  fb_ctx.width      = width;
  fb_ctx.height     = height;
  fb_ctx.pitch      = pitch;
  fb_ctx.bytes_pp   = bytes_pp_from_bpp(bpp);
  fb_ctx.cell_w     = FONT_W;
  fb_ctx.cell_h     = FONT_H;
  fb_ctx.margin_x   = 0;
  fb_ctx.margin_y   = 0;
  fb_ctx.cols       = (int)(width / (u64)fb_ctx.cell_w);
  fb_ctx.rows       = (int)(height / (u64)fb_ctx.cell_h);
  fb_ctx.default_fg = 0xcdd6f4u; /* Catppuccin Mocha Text */
  fb_ctx.default_bg = 0x1e1e2eu; /* Catppuccin Mocha Base */
  fb_ctx.cur_fg     = fb_ctx.default_fg;
  fb_ctx.cur_bg     = fb_ctx.default_bg;
  fb_ctx.cx = fb_ctx.cy = 0;
  fb_ctx.utf8_rem       = 0;
  fb_ctx.blink_ticks    = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on       = 1;
  fb_ctx.cell_blink_on  = 1;
  fb_ctx.cur_attr       = 0;
  fb_ctx.cursor_visible = 1;
  fb_ctx.yielded        = false;
  fb_ctx.in_head = fb_ctx.in_tail = 0;

  size_t total = (size_t)fb_ctx.rows * (size_t)fb_ctx.cols;
  fb_ctx.cells = (fb_cell_t *)kmalloc(total * sizeof(fb_cell_t));
  if(!fb_ctx.cells)
    return false;
  for(size_t i = 0; i < total; i++) {
    fb_ctx.cells[i].cp    = (u32)' ';
    fb_ctx.cells[i].fg    = fb_ctx.default_fg;
    fb_ctx.cells[i].bg    = fb_ctx.default_bg;
    fb_ctx.cells[i].attr  = 0;
    fb_ctx.cells[i].dirty = 0;
  }

  scrollback_alloc_for(fb_ctx.cols);
  flush_ci_ensure(fb_ctx.cols);
  return true;
}

void fb_console_write_begin(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  scrollback_exit(); /* any write returns to live view */
  fb_ctx.batch_r0 = fb_ctx.rows;
  fb_ctx.batch_r1 = -1;
  caret_invalidate_in_batch();
  fb_ctx.in_batch = true;
}

void fb_console_write_raw(const void *buf, size_t len)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  const u8 *p = (const u8 *)buf;
  for(size_t i = 0; i < len; i++)
    feed_byte(p[i]);
}

void fb_console_write_end(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  fb_ctx.in_batch = false;
  flush_pending_scroll();
  flush_batch();
  fb_ctx.blink_ticks = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on    = 1;
  caret_paint();
}

void fb_console_write(const void *buf, size_t len)
{
  fb_console_write_begin();
  fb_console_write_raw(buf, len);
  fb_console_write_end();
}

void fb_console_push_input(u8 byte)
{
  unsigned int next = (fb_ctx.in_tail + 1u) % INPUT_RING;
  if(next == fb_ctx.in_head)
    return; /* drop on overflow */
  fb_ctx.in_buf[fb_ctx.in_tail] = byte;
  fb_ctx.in_tail                = next;
  fb_ctx.blink_ticks            = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on               = 1;
}

size_t fb_console_read(void *buf, size_t max)
{
  u8    *out = (u8 *)buf;
  size_t n   = 0;
  while(n < max && fb_ctx.in_head != fb_ctx.in_tail) {
    out[n++]       = fb_ctx.in_buf[fb_ctx.in_head];
    fb_ctx.in_head = (fb_ctx.in_head + 1u) % INPUT_RING;
  }
  return n;
}

void fb_console_tick(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  if(fb_ctx.blink_ticks == 0) {
    fb_ctx.blink_on      = (u8)!fb_ctx.blink_on;
    fb_ctx.cell_blink_on = fb_ctx.blink_on;
    fb_ctx.blink_ticks   = FB_BLINK_PERIOD_TICKS;

    fb_ctx.batch_r0 = fb_ctx.rows;
    fb_ctx.batch_r1 = -1;
    for(int r = 0; r < fb_ctx.rows; r++) {
      for(int c = 0; c < fb_ctx.cols; c++) {
        fb_cell_t *cell =
            &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c];
        if(cell->attr & FB_ATTR_BLINK) {
          cell->dirty = 1;
          if(r < fb_ctx.batch_r0)
            fb_ctx.batch_r0 = r;
          if(r > fb_ctx.batch_r1)
            fb_ctx.batch_r1 = r;
        }
      }
    }
    if(fb_ctx.batch_r0 <= fb_ctx.batch_r1) {
      flush_batch();
      /* The re-blit may have painted over the pointer; force a full redraw. */
      mouse_cursor_drop();
    }

    caret_refresh();
  } else {
    fb_ctx.blink_ticks--;
  }

  /* Redraw every tick: mouse_cursor_render() early-outs when the pointer hasn't
   * moved, so this is free at rest and as smooth as the tick rate in motion. */
  mouse_cursor_render();
}

int fb_console_set_atlas(const fb_console_atlas_t *meta)
{
  if(!meta || !atlas_meta_is_sane(meta))
    return -1;

  if(!vmm_is_user_range((void *)(u64)meta->pixels_user, meta->pixels_size))
    return -1;
  u64 cp_bytes = (u64)meta->n_cp * sizeof(u32);
  if(!vmm_is_user_range((void *)(u64)meta->cp_map_user, cp_bytes))
    return -1;

  u8  *new_pixels = (u8 *)kmalloc(meta->pixels_size);
  u32 *new_cp_map = (u32 *)kmalloc(cp_bytes);
  if(!new_pixels || !new_cp_map) {
    if(new_pixels)
      kfree(new_pixels);
    if(new_cp_map)
      kfree(new_cp_map);
    return -1;
  }
  /* Current proc's CR3 is active during the syscall: user VAs are mapped. */
  kmemcpy(new_pixels, (const void *)(u64)meta->pixels_user, meta->pixels_size);
  kmemcpy(new_cp_map, (const void *)(u64)meta->cp_map_user, cp_bytes);

  /* Release any prior atlas. */
  if(fb_ctx.atlas_pixels)
    kfree(fb_ctx.atlas_pixels);
  if(fb_ctx.atlas_cp_map)
    kfree(fb_ctx.atlas_cp_map);

  fb_ctx.atlas_pixels      = new_pixels;
  fb_ctx.atlas_cp_map      = new_cp_map;
  fb_ctx.atlas_cell_w      = meta->cell_w;
  fb_ctx.atlas_cell_h      = meta->cell_h;
  fb_ctx.atlas_stride      = meta->stride_bytes;
  fb_ctx.atlas_bpp         = meta->bpp;
  fb_ctx.atlas_n_glyphs    = meta->n_glyphs;
  fb_ctx.atlas_n_cp        = meta->n_cp;
  fb_ctx.atlas_fallback    = meta->fallback_idx;
  fb_ctx.atlas_bold_base   = meta->bold_offset;
  fb_ctx.atlas_italic_base = meta->italic_offset;
  fb_ctx.atlas_active      = true;

  /* Adopt the atlas's cell pixel size and reflow the grid. Cursor + saved
   * cursor get clamped into the new geometry; existing content is discarded
   * (cleared to a fresh grid) because cell coordinates don't survive a
   * cols/rows change in any well-defined way. */
  int new_cell_w = (int)meta->cell_w;
  int new_cell_h = (int)meta->cell_h;
  int new_marg_x = FB_CONSOLE_MARGIN;
  int new_marg_y = FB_CONSOLE_MARGIN;
  int old_cols   = fb_ctx.cols;
  int old_rows   = fb_ctx.rows;
  if(new_cell_w != fb_ctx.cell_w || new_cell_h != fb_ctx.cell_h ||
     new_marg_x != fb_ctx.margin_x || new_marg_y != fb_ctx.margin_y) {
    int new_cols =
        (int)((fb_ctx.width - 2u * (u64)new_marg_x) / (u64)new_cell_w);
    int new_rows =
        (int)((fb_ctx.height - 2u * (u64)new_marg_y) / (u64)new_cell_h);
    if(new_cols < 1)
      new_cols = 1;
    if(new_rows < 1)
      new_rows = 1;
    size_t     total = (size_t)new_cols * (size_t)new_rows;
    fb_cell_t *nc    = (fb_cell_t *)kmalloc(total * sizeof(fb_cell_t));
    if(nc) {
      for(size_t i = 0; i < total; i++) {
        nc[i].cp   = (u32)' ';
        nc[i].fg   = fb_ctx.default_fg;
        nc[i].bg   = fb_ctx.default_bg;
        nc[i].attr = 0;
      }
      if(fb_ctx.cells)
        kfree(fb_ctx.cells);
      fb_ctx.cells    = nc;
      fb_ctx.cols     = new_cols;
      fb_ctx.rows     = new_rows;
      fb_ctx.cell_w   = new_cell_w;
      fb_ctx.cell_h   = new_cell_h;
      fb_ctx.margin_x = new_marg_x;
      fb_ctx.margin_y = new_marg_y;
      fb_ctx.cx = fb_ctx.cy = 0;
      fb_ctx.saved_cx = fb_ctx.saved_cy = 0;
      scrollback_alloc_for(new_cols);
      flush_ci_ensure(new_cols);
      /* Wipe stale pixels left around the old grid. */
      for(u32 y = 0; y < fb_ctx.height; y++)
        for(u32 x = 0; x < fb_ctx.width; x++)
          fb_put_pixel(x, y, fb_ctx.default_bg);
    }
    /* If kmalloc fails, fall through and repaint with the existing grid;
     * the atlas blit will just clip to fb_ctx.cell_w/cell_h as before. */
  }

  /* Repaint the whole grid through the new path. The cursor cell, if any,
   * was overwritten by that loop so its tracked position is now stale; any
   * pending scroll is moot because we just repainted everything. */
  scrollback_drop_pending();
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      blit_cell(c, r);
  caret_clear_drawn();
  caret_paint();

  /* Wake every TUI so they re-query TIOCGWINSZ and redraw at the real grid
   * size. Skipped when the grid stayed the same (e.g. atlas reloaded with
   * identical metrics) — no point waking anyone in that case. */
  if(fb_ctx.cols != old_cols || fb_ctx.rows != old_rows)
    proc_signal_broadcast(SIGWINCH);
  return 0;
}

void fb_console_get_size(int *cols, int *rows)
{
  if(cols)
    *cols = fb_ctx.cols;
  if(rows)
    *rows = fb_ctx.rows;
}

/**
 * @brief Pack live console geometry into a Linux winsize for a TIOCGWINSZ
 * answer.
 *
 * Pre-seeded with the VT100 fallback and re-clamped after the query because
 * @ref fb_console_get_size returns the raw @c ctx fields without sanitising —
 * if a TTY ioctl ever races the boot path before @ref fb_console_init runs we
 * still hand userspace something a TUI can divide by.
 *
 * Pixel dimensions are zeroed: the grid is a character matrix, no caller has
 * a use for the underlying pixel span and reporting a wrong value would mislead
 * TUIs into laying out against a non-existent geometry.
 *
 * @param out  Destination winsize. Caller-owned, no NULL guard (a kernel
 *             caller passing NULL is a logic bug — fail fast via crash).
 */
void fb_console_fill_winsize(k_winsize_t *out)
{
  int cols = KTERM_WINSIZE_FALLBACK_COLS;
  int rows = KTERM_WINSIZE_FALLBACK_ROWS;
  fb_console_get_size(&cols, &rows);
  if(cols <= 0)
    cols = KTERM_WINSIZE_FALLBACK_COLS;
  if(rows <= 0)
    rows = KTERM_WINSIZE_FALLBACK_ROWS;
  out->row    = rows;
  out->col    = cols;
  out->xpixel = 0;
  out->ypixel = 0;
}

bool fb_console_app_cursor_keys(void)
{
  return fb_ctx.app_cursor_keys;
}

void fb_console_yield(void)
{
  fb_ctx.yielded = true;
  /* User owns the pixels; don't XOR-erase a stale pos when we come back. */
  mouse_cursor_drop();
}

void fb_console_reclaim(void)
{
  fb_ctx.yielded = false;
  mouse_cursor_drop();
  if(!fb_ctx.cells)
    return;
  /* Fill the whole framebuffer with the theme background first: a yielding app
   * (e.g. doom) may have left arbitrary pixels in the margins outside the cell
   * grid, which re-blitting cells alone would not cover. */
  if(fb_ctx.base && fb_ctx.bytes_pp == 4)
    for(u32 y = 0; y < fb_ctx.height; y++)
      fill32(
          (volatile u32 *)(fb_ctx.base + (u64)y * fb_ctx.pitch),
          BGRA_OPAQUE_ALPHA | fb_ctx.default_bg, fb_ctx.width
      );
  scrollback_drop_pending();
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      blit_cell(c, r);
  caret_clear_drawn();
  caret_paint();
}
