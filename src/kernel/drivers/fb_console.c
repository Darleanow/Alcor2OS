/**
 * @file src/kernel/drivers/fb_console.c
 * @brief Kernel framebuffer text console (runtime terminal).
 *
 * Replaces the userspace fb_tty.c. Cell grid + UTF-8 + ANSI/CSI parser live in
 * kernel; glyph rendering is either compiled-in CP437 bitmap (used at boot and
 * as fallback) or a userspace-supplied Fira atlas registered through
 * @c fb_console_set_atlas.
 *
 * Phasing: this file is being built up incrementally. The current state covers
 * cell grid + UTF-8 + plain bitmap blit. ANSI/CSI parser, atlas support, and
 * yield/reclaim are scaffolded (with TODO markers) and will land in follow-up
 * commits on the same branch.
 */

#include "../../drivers/console/font.h"
#include <alcor2/drivers/fb_console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>

/* ---- Static state ------------------------------------------------------ */

typedef struct
{
  u32 cp;   /**< Unicode codepoint at this cell. */
  u32 fg;   /**< RGB foreground. */
  u32 bg;   /**< RGB background. */
  u16 attr; /**< SGR attribute bits (reserved). */
  u16 _pad;
} fb_cell_t;

#define INPUT_RING 256

static struct
{
  /* Framebuffer */
  volatile u8 *base;
  u64          width, height, pitch;
  u8           bytes_pp;

  /* Cell grid (kmalloc'd at init). */
  fb_cell_t *cells;
  int        rows, cols; /* in cells */
  int        cx, cy;     /* cursor in cell coords */

  /* Default colors (used for SGR resets). */
  u32 default_fg, default_bg;
  u32 cur_fg, cur_bg;

  /* UTF-8 decoder state. */
  u32 utf8_partial;
  u8  utf8_rem;

  /* Cursor blink: tick counter (decrements from 50 at 100 Hz → ~2 Hz). */
  u8 blink_ticks;
  u8 blink_on;
  u8 cursor_visible;

  /* fb yielded to a userspace mmap-er (e.g. doom). */
  bool yielded;

  /* Input ring (keyboard → reader). */
  u8           in_buf[INPUT_RING];
  unsigned int in_head, in_tail;
} ctx;

/* ---- Pixel-level helpers (mirror of console.c, kept local) ------------- */

#define FONT_W 8
#define FONT_H 16

static u8 bytes_pp_from_bpp(u16 bpp)
{
  switch(bpp) {
  case 32:
    return 4;
  case 24:
    return 3;
  case 16:
    return 2;
  default:
    return 4;
  }
}

static void fb_put_pixel(u32 x, u32 y, u32 color)
{
  if(x >= ctx.width || y >= ctx.height)
    return;
  volatile u8 *p = ctx.base + (u64)y * ctx.pitch + (u64)x * ctx.bytes_pp;
  switch(ctx.bytes_pp) {
  case 4:
    *(volatile u32 *)p = color | 0xFF000000u;
    return;
  case 3:
    p[0] = (u8)(color & 0xffu);
    p[1] = (u8)((color >> 8) & 0xffu);
    p[2] = (u8)((color >> 16) & 0xffu);
    return;
  case 2: {
    u32 r      = (color >> 16) & 0xffu;
    u32 g      = (color >> 8) & 0xffu;
    u32 b      = color & 0xffu;
    u16 rgb565 = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    p[0]       = (u8)(rgb565 & 0xffu);
    p[1]       = (u8)(rgb565 >> 8);
    return;
  }
  default:
    return;
  }
}

/* ---- Glyph rendering --------------------------------------------------- */

/** Blit one cell using the compiled-in CP437 bitmap font.
 *  Codepoints > 0xff fall back to '?'. Atlas path (Fira) will short-circuit
 *  here in a later commit. */
static void blit_cell(int col, int row)
{
  const fb_cell_t *c = &ctx.cells[(size_t)row * (size_t)ctx.cols + (size_t)col];
  u32              cp = c->cp;
  u8               glyph_idx;
  if(cp <= 0xffu)
    glyph_idx = (u8)cp;
  else
    glyph_idx = (u8)'?';

  int gi = font_glyph_index(glyph_idx);
  if(gi < 0)
    gi = font_glyph_index((u8)'?');
  if(gi < 0)
    return;

  const u8 *glyph = font_latin1[gi];
  u32       px    = (u32)col * (u32)FONT_W;
  u32       py    = (u32)row * (u32)FONT_H;

  for(int gy = 0; gy < FONT_H; gy++) {
    u8 bits = glyph[gy];
    for(int gx = 0; gx < FONT_W; gx++) {
      u32 color = ((bits & (0x80u >> gx)) != 0) ? c->fg : c->bg;
      fb_put_pixel(px + (u32)gx, py + (u32)gy, color);
    }
  }
}

/* Scroll the cell grid up by one row; clear bottom row. */
static void scroll_one(void)
{
  size_t row_bytes = (size_t)ctx.cols * sizeof(fb_cell_t);
  for(int r = 0; r < ctx.rows - 1; r++)
    kmemcpy(
        &ctx.cells[(size_t)r * (size_t)ctx.cols],
        &ctx.cells[(size_t)(r + 1) * (size_t)ctx.cols], row_bytes
    );
  for(int c = 0; c < ctx.cols; c++) {
    fb_cell_t *cell =
        &ctx.cells[(size_t)(ctx.rows - 1) * (size_t)ctx.cols + (size_t)c];
    cell->cp   = (u32)' ';
    cell->fg   = ctx.cur_fg;
    cell->bg   = ctx.cur_bg;
    cell->attr = 0;
  }
  /* Repaint everything — full-screen redraw on scroll. */
  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      blit_cell(c, r);
}

/* ---- Codepoint emission ------------------------------------------------ */

static void put_cp_at_cursor(u32 cp)
{
  if(ctx.cx >= ctx.cols) {
    ctx.cx = 0;
    ctx.cy++;
    if(ctx.cy >= ctx.rows) {
      scroll_one();
      ctx.cy = ctx.rows - 1;
    }
  }
  fb_cell_t *c = &ctx.cells[(size_t)ctx.cy * (size_t)ctx.cols + (size_t)ctx.cx];
  c->cp        = cp;
  c->fg        = ctx.cur_fg;
  c->bg        = ctx.cur_bg;
  c->attr      = 0;
  blit_cell(ctx.cx, ctx.cy);
  ctx.cx++;
}

static void handle_control(u8 b)
{
  switch(b) {
  case '\n':
    ctx.cx = 0;
    ctx.cy++;
    if(ctx.cy >= ctx.rows) {
      scroll_one();
      ctx.cy = ctx.rows - 1;
    }
    return;
  case '\r':
    ctx.cx = 0;
    return;
  case '\b':
    if(ctx.cx > 0)
      ctx.cx--;
    return;
  case '\t':
    ctx.cx = (ctx.cx + 8) & ~7;
    if(ctx.cx >= ctx.cols)
      ctx.cx = ctx.cols - 1;
    return;
  default:
    return;
  }
}

/* UTF-8 decoder, byte at a time. Emits a codepoint at the cursor when one
 * completes; ANSI/CSI sequences are picked off ahead of this in
 * fb_console_write (TODO — currently fed straight through). */
static void feed_utf8(u8 b)
{
  if(ctx.utf8_rem == 0) {
    if(b < 0x20u || b == 0x7fu) {
      handle_control(b);
      return;
    }
    if(b < 0x80u) {
      put_cp_at_cursor((u32)b);
      return;
    }
    if((b & 0xe0u) == 0xc0u) {
      ctx.utf8_partial = (u32)(b & 0x1fu);
      ctx.utf8_rem     = 1;
      return;
    }
    if((b & 0xf0u) == 0xe0u) {
      ctx.utf8_partial = (u32)(b & 0x0fu);
      ctx.utf8_rem     = 2;
      return;
    }
    if((b & 0xf8u) == 0xf0u) {
      ctx.utf8_partial = (u32)(b & 0x07u);
      ctx.utf8_rem     = 3;
      return;
    }
    /* Stray continuation / invalid lead — show '?'. */
    put_cp_at_cursor((u32)'?');
    return;
  }
  if((b & 0xc0u) != 0x80u) {
    /* Broken sequence; recover by replaying this byte fresh. */
    ctx.utf8_rem = 0;
    put_cp_at_cursor((u32)'?');
    feed_utf8(b);
    return;
  }
  ctx.utf8_partial = (ctx.utf8_partial << 6) | (u32)(b & 0x3fu);
  ctx.utf8_rem--;
  if(ctx.utf8_rem == 0) {
    u32 cp = ctx.utf8_partial;
    if(cp <= 0x10ffffu)
      put_cp_at_cursor(cp);
    else
      put_cp_at_cursor((u32)'?');
  }
}

/* ---- Public API -------------------------------------------------------- */

bool fb_console_init(void *fb, u64 width, u64 height, u64 pitch, u16 bpp)
{
  ctx.base       = (volatile u8 *)fb;
  ctx.width      = width;
  ctx.height     = height;
  ctx.pitch      = pitch;
  ctx.bytes_pp   = bytes_pp_from_bpp(bpp);
  ctx.cols       = (int)(width / FONT_W);
  ctx.rows       = (int)(height / FONT_H);
  ctx.default_fg = 0xFFFFFFu;
  ctx.default_bg = 0x000000u;
  ctx.cur_fg     = ctx.default_fg;
  ctx.cur_bg     = ctx.default_bg;
  ctx.cx = ctx.cy    = 0;
  ctx.utf8_rem       = 0;
  ctx.blink_ticks    = 50;
  ctx.blink_on       = 1;
  ctx.cursor_visible = 1;
  ctx.yielded        = false;
  ctx.in_head = ctx.in_tail = 0;

  size_t total = (size_t)ctx.rows * (size_t)ctx.cols;
  ctx.cells    = (fb_cell_t *)kmalloc(total * sizeof(fb_cell_t));
  if(!ctx.cells)
    return false;
  for(size_t i = 0; i < total; i++) {
    ctx.cells[i].cp   = (u32)' ';
    ctx.cells[i].fg   = ctx.default_fg;
    ctx.cells[i].bg   = ctx.default_bg;
    ctx.cells[i].attr = 0;
  }
  return true;
}

void fb_console_write(const void *buf, size_t len)
{
  if(ctx.yielded || !ctx.cells)
    return;
  const u8 *p = (const u8 *)buf;
  /* TODO: ANSI/CSI state machine layer between here and feed_utf8 — coming
   * in the next commit. For now we strip ESC sequences so they don't paint
   * literal '?' glyphs. */
  for(size_t i = 0; i < len; i++)
    feed_utf8(p[i]);
  /* Activity → cursor blink "on". */
  ctx.blink_ticks = 50;
  ctx.blink_on    = 1;
}

void fb_console_push_input(u8 byte)
{
  unsigned int next = (ctx.in_tail + 1u) % INPUT_RING;
  if(next == ctx.in_head)
    return; /* drop on overflow */
  ctx.in_buf[ctx.in_tail] = byte;
  ctx.in_tail             = next;
  ctx.blink_ticks         = 50;
  ctx.blink_on            = 1;
}

size_t fb_console_read(void *buf, size_t max)
{
  u8    *out = (u8 *)buf;
  size_t n   = 0;
  while(n < max && ctx.in_head != ctx.in_tail) {
    out[n++]    = ctx.in_buf[ctx.in_head];
    ctx.in_head = (ctx.in_head + 1u) % INPUT_RING;
  }
  return n;
}

void fb_console_tick(void)
{
  if(ctx.yielded || !ctx.cells)
    return;
  if(ctx.blink_ticks == 0) {
    ctx.blink_on    = (u8)!ctx.blink_on;
    ctx.blink_ticks = 50;
    /* TODO: repaint just the cursor cell with inverted fg/bg. */
  } else {
    ctx.blink_ticks--;
  }
}

int fb_console_set_atlas(const fb_console_atlas_t *meta)
{
  (void)meta;
  /* TODO: map userspace pages, switch blit_cell to atlas path. */
  return -1;
}

void fb_console_yield(void)
{
  ctx.yielded = true;
}

void fb_console_reclaim(void)
{
  ctx.yielded = false;
  if(!ctx.cells)
    return;
  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      blit_cell(c, r);
}
