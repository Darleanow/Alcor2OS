/**
 * @file src/kernel/drivers/fb_console.c
 * @brief Runtime framebuffer text console with UTF-8 and ANSI/CSI support.
 *
 * Maintains an in-RAM cell grid, blits glyphs via the compiled-in CP437 bitmap
 * or a userspace-supplied Fira atlas, and renders scrollback history.
 * Takes over the framebuffer from the early boot logger once kmalloc is up.
 */

#include <drivers/console/font.h>
#include <alcor2/drivers/fb_console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>
/* Forward declaration — avoids pulling in proc/signal.h for one call. */
#define SIGWINCH 28
void proc_signal_broadcast(int signum);
#include <alcor2/types.h>

typedef struct
{
  u32 cp;    /**< Unicode codepoint at this cell. */
  u32 fg;    /**< RGB foreground. */
  u32 bg;    /**< RGB background. */
  u16 attr;  /**< SGR attribute bits (FB_ATTR_*). */
  u16 dirty; /**< Batch-blit dirty flag: 1 = needs blit at flush_batch(). */
} fb_cell_t;

#define FB_ATTR_BLINK     (1u << 0)
#define FB_ATTR_BOLD      (1u << 1)
#define FB_ATTR_ITALIC    (1u << 2)
#define FB_ATTR_UNDERLINE (1u << 3)
#define FB_ATTR_REVERSE   (1u << 4)

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

  /* Cell pixel dimensions. Defaults to the compiled-in CP437 bitmap size;
   * an atlas submission can replace them with its own cell_w / cell_h. */
  int cell_w, cell_h;

  /* Grid offset in pixels from the framebuffer origin (top-left padding).
   * Zero at boot so the bitmap path fills the screen; bumped to
   * FB_CONSOLE_MARGIN once an atlas takes over. */
  int margin_x, margin_y;

  /* Default colors (used for SGR resets). */
  u32 default_fg, default_bg;
  u32 cur_fg, cur_bg;
  u8  cur_attr;       /* current SGR attribute bits (FB_ATTR_*) */
  u8  cell_blink_on;  /* cell blink display phase: 1=visible, 0=hidden */

  /* UTF-8 decoder state. */
  u32 utf8_partial;
  u8  utf8_rem;

  /* ANSI escape-sequence state machine.
   *   0: NORMAL — bytes feed straight through UTF-8 → cell
   *   1: ESC    — saw 0x1b, waiting for the next byte
   *   2: CSI    — inside `ESC [`, accumulating params into esc_buf
   *   3: G0SET  — inside `ESC (`, waiting for the charset designator */
  u8   esc_state;
  u8   esc_len;
  char esc_buf[64];
  u8   g0_acs;  /* 1 once `ESC ( 0` has selected DEC Special Graphics. */
  u32  last_cp; /* last emitted codepoint, replayed by CSI REP (`b`). */

  /* Saved cursor for ESC 7/8 + CSI s/u. */
  int saved_cx, saved_cy;

  /* Cursor blink: tick counter (decrements from 50 at 100 Hz → ~2 Hz). */
  u8 blink_ticks;
  u8 blink_on;
  u8 cursor_visible;

  /* DECCKM: when set, cursor keys send SS3 (\EOA) instead of CSI (\E[A).
   * ncurses' keypad(TRUE) toggles this via the terminfo smkx string. */
  bool app_cursor_keys;

  /* fb yielded to a userspace mmap-er (e.g. doom). */
  bool yielded;

  /* Batch-blit mode: when true, put_cp_at_cursor/erase_rect mark cell
   * dirty rather than blitting immediately. flush_batch() drains them.
   * batch_r0..batch_r1 track the inclusive dirty row range so flush_batch
   * can skip the full 80×25 scan when only a few rows changed. */
  bool in_batch;
  int  batch_r0, batch_r1;

  /* Input ring (keyboard → reader). */
  u8           in_buf[INPUT_RING];
  unsigned int in_head, in_tail;

  /* Userspace-submitted glyph atlas; bitmap font is the fallback. */
  bool atlas_active;
  u8  *atlas_pixels; /* kernel buffer copy. */
  u32 *atlas_cp_map; /* kernel buffer copy: u32[atlas_n_cp]. */
  u32  atlas_cell_w, atlas_cell_h;
  u32  atlas_stride;
  u32  atlas_bpp;
  u32  atlas_n_glyphs;
  u32  atlas_n_cp;
  u32  atlas_fallback;
  u32  atlas_bold_base;   /* first bold glyph slot; 0 = no bold atlas   */
  u32  atlas_italic_base; /* first italic glyph slot; 0 = no italic atlas */
} ctx;

#define ATLAS_NO_GLYPH    0xFFFFFFFFu
#define SCROLLBACK_ROWS   500

#define FB_CONSOLE_MARGIN 20

#define FONT_W            8
#define FONT_H            16

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

/** Resolve a codepoint to an atlas glyph index, or ATLAS_NO_GLYPH if absent. */
static u32 atlas_lookup(u32 cp)
{
  if(!ctx.atlas_active)
    return ATLAS_NO_GLYPH;
  if(cp < ctx.atlas_n_cp) {
    u32 idx = ctx.atlas_cp_map[cp];
    if(idx < ctx.atlas_n_glyphs)
      return idx;
  }
  return ctx.atlas_fallback;
}

static u32 atlas_lookup_attr(u32 cp, u16 attr)
{
  u32 idx = atlas_lookup(cp);
  if(idx == ATLAS_NO_GLYPH)
    return ATLAS_NO_GLYPH;
  if((attr & FB_ATTR_BOLD) && ctx.atlas_bold_base > 0u) {
    u32 bi = idx + ctx.atlas_bold_base;
    if(bi < ctx.atlas_n_glyphs) return bi;
  } else if((attr & FB_ATTR_ITALIC) && ctx.atlas_italic_base > 0u) {
    u32 ii = idx + ctx.atlas_italic_base;
    if(ii < ctx.atlas_n_glyphs) return ii;
  }
  return idx;
}

static inline void fill32(volatile u32 *dst, u32 val, u32 n)
{
  u32 *d = (u32 *)(uintptr_t)dst;
  __asm__ volatile("rep stosl" : "+D"(d), "+c"(n) : "a"(val) : "memory");
}

/* Blend one row of glyph pixels into the framebuffer.  bypp==1: alpha-only
 * atlas (typical FreeType grayscale).  bypp==4: RGBA atlas (px[3] = alpha).
 * The fast-paths for a=0 and a=255 avoid the multiply for solid/transparent
 * pixels, which covers the majority of glyph coverage maps. */
static inline void blend_glyph_row(
    volatile u32 *dst, const u8 *src, u32 n, u32 bypp,
    u32 fg_r, u32 fg_g, u32 fg_b,
    u32 bg_r, u32 bg_g, u32 bg_b,
    u32 fg_pk, u32 bg_pk)
{
  if(bypp == 1u) {
    for(u32 gx = 0; gx < n; gx++) {
      u32 a = src[gx];
      if(!a)      { dst[gx] = bg_pk; continue; }
      if(a==255u) { dst[gx] = fg_pk; continue; }
      u32 inv = 255u - a;
      dst[gx] = 0xFF000000u
              | ((fg_r*a + bg_r*inv + 128u) >> 8) << 16
              | ((fg_g*a + bg_g*inv + 128u) >> 8) << 8
              |  (fg_b*a + bg_b*inv + 128u) >> 8;
    }
  } else {
    for(u32 gx = 0; gx < n; gx++) {
      const u8 *px = src + gx * bypp;
      u32 a = (bypp == 4u) ? (u32)px[3] : (u32)px[0];
      if(!a)      { dst[gx] = bg_pk; continue; }
      if(a==255u) { dst[gx] = fg_pk; continue; }
      u32 inv = 255u - a;
      dst[gx] = 0xFF000000u
              | ((fg_r*a + bg_r*inv + 128u) >> 8) << 16
              | ((fg_g*a + bg_g*inv + 128u) >> 8) << 8
              |  (fg_b*a + bg_b*inv + 128u) >> 8;
    }
  }
}

static void blit_cell_data(const fb_cell_t *c, int col, int row)
{
  u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
  u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

  u32 px_x = (u32)ctx.margin_x + (u32)col * (u32)ctx.cell_w;
  u32 px_y = (u32)ctx.margin_y + (u32)row * (u32)ctx.cell_h;

  if(ctx.atlas_active) {
    if(ctx.bytes_pp == 4) {
      u32 bg_pk = 0xFF000000u | eff_bg;
      u32 fg_pk = 0xFF000000u | eff_fg;

      if(c->cp == ' ') {
        for(u32 gy = 0; gy < (u32)ctx.cell_h; gy++) {
          volatile u32 *dst = (volatile u32 *)(ctx.base
                             + (u64)(px_y + gy) * ctx.pitch + (u64)px_x * 4u);
          fill32(dst, bg_pk, (u32)ctx.cell_w);
        }
        goto post;
      }

      u32 idx = atlas_lookup_attr(c->cp, c->attr);
      if(idx != ATLAS_NO_GLYPH && idx < ctx.atlas_n_glyphs) {
        u32 fg_r = (eff_fg >> 16) & 0xffu, fg_g = (eff_fg >> 8) & 0xffu, fg_b = eff_fg & 0xffu;
        u32 bg_r = (eff_bg >> 16) & 0xffu, bg_g = (eff_bg >> 8) & 0xffu, bg_b = eff_bg & 0xffu;
        const u8 *glyph      = ctx.atlas_pixels
                             + (size_t)idx * (size_t)ctx.atlas_cell_h * (size_t)ctx.atlas_stride;
        u32       atlas_bypp = (ctx.atlas_bpp + 7u) / 8u;
        u32       cell_h     = ctx.atlas_cell_h < (u32)ctx.cell_h ? ctx.atlas_cell_h : (u32)ctx.cell_h;
        u32       cell_w     = ctx.atlas_cell_w < (u32)ctx.cell_w ? ctx.atlas_cell_w : (u32)ctx.cell_w;

        for(u32 gy = 0; gy < cell_h; gy++) {
          const u8     *src = glyph + (size_t)gy * (size_t)ctx.atlas_stride;
          volatile u32 *dst = (volatile u32 *)(ctx.base
                             + (u64)(px_y + gy) * ctx.pitch + (u64)px_x * 4u);
          blend_glyph_row(dst, src, cell_w, atlas_bypp,
                          fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, fg_pk, bg_pk);
        }
        goto post;
      }
    } else {
      u32 idx = atlas_lookup_attr(c->cp, c->attr);
      if(idx != ATLAS_NO_GLYPH && idx < ctx.atlas_n_glyphs) {
        u32 fg_r = (eff_fg >> 16) & 0xffu, fg_g = (eff_fg >> 8) & 0xffu, fg_b = eff_fg & 0xffu;
        u32 bg_r = (eff_bg >> 16) & 0xffu, bg_g = (eff_bg >> 8) & 0xffu, bg_b = eff_bg & 0xffu;
        const u8 *glyph      = ctx.atlas_pixels
                             + (size_t)idx * (size_t)ctx.atlas_cell_h * (size_t)ctx.atlas_stride;
        u32       atlas_bypp = (ctx.atlas_bpp + 7u) / 8u;
        u32       cell_h     = ctx.atlas_cell_h < (u32)ctx.cell_h ? ctx.atlas_cell_h : (u32)ctx.cell_h;
        u32       cell_w     = ctx.atlas_cell_w < (u32)ctx.cell_w ? ctx.atlas_cell_w : (u32)ctx.cell_w;
        for(u32 gy = 0; gy < cell_h; gy++) {
          const u8 *src = glyph + (size_t)gy * (size_t)ctx.atlas_stride;
          for(u32 gx = 0; gx < cell_w; gx++) {
            const u8 *px = src + gx * atlas_bypp;
            u32 a = (atlas_bypp == 4u) ? (u32)px[3] : (u32)px[0];
            if(!a)      { fb_put_pixel(px_x+gx, px_y+gy, eff_bg); continue; }
            if(a==255u) { fb_put_pixel(px_x+gx, px_y+gy, eff_fg); continue; }
            u32 inv = 255u - a;
            u32 r = (fg_r*a + bg_r*inv + 128u) >> 8;
            u32 g = (fg_g*a + bg_g*inv + 128u) >> 8;
            u32 b = (fg_b*a + bg_b*inv + 128u) >> 8;
            fb_put_pixel(px_x+gx, px_y+gy, (r << 16) | (g << 8) | b);
          }
        }
        goto post;
      }
    }
  }

  for(int gy = 0; gy < ctx.cell_h; gy++)
    for(int gx = 0; gx < ctx.cell_w; gx++)
      fb_put_pixel(px_x + (u32)gx, px_y + (u32)gy, eff_bg);
  {
    u32 cp        = c->cp;
    u8  glyph_idx = (cp <= 0xffu) ? (u8)cp : (u8)'?';
    int gi        = font_glyph_index(glyph_idx);
    if(gi < 0) gi = font_glyph_index((u8)'?');
    if(gi >= 0) {
      int       gx_off = (ctx.cell_w > FONT_W) ? (ctx.cell_w - FONT_W) / 2 : 0;
      int       gy_off = (ctx.cell_h > FONT_H) ? (ctx.cell_h - FONT_H) / 2 : 0;
      const u8 *glyph  = font_latin1[gi];
      for(int gy = 0; gy < FONT_H && gy < ctx.cell_h; gy++) {
        u8 bits = glyph[gy];
        for(int gx = 0; gx < FONT_W && gx < ctx.cell_w; gx++)
          if((bits & (0x80u >> gx)) != 0)
            fb_put_pixel(px_x+(u32)(gx_off+gx), px_y+(u32)(gy_off+gy), eff_fg);
      }
    }
  }

post:
  if((c->attr & FB_ATTR_UNDERLINE) && ctx.base && ctx.bytes_pp == 4) {
    u32 uline_color = 0xFF000000u | eff_fg;
    u32 uline_y     = px_y + (u32)ctx.cell_h - 2u;
    for(u32 uy = uline_y; uy < px_y + (u32)ctx.cell_h; uy++) {
      volatile u32 *dst = (volatile u32 *)(ctx.base + (u64)uy * ctx.pitch + (u64)px_x * 4u);
      fill32(dst, uline_color, (u32)ctx.cell_w);
    }
  }
}

static void blit_cell(int col, int row)
{
  blit_cell_data(&ctx.cells[(size_t)row * (size_t)ctx.cols + (size_t)col], col, row);
}

/* Last drawn cursor cell. -1 means "no cursor on screen right now", which
 * cursor_erase treats as a no-op. Tracked so a moved cursor can wipe its
 * previous block by re-blitting just that one cell. */
static int s_cursor_drawn_x = -1;
static int s_cursor_drawn_y = -1;

/** Re-blit the cell that currently shows the cursor block, restoring the
 *  glyph that was underneath. */
static void cursor_erase(void)
{
  if(s_cursor_drawn_x < 0 || s_cursor_drawn_y < 0)
    return;
  if(ctx.cells && s_cursor_drawn_y < ctx.rows && s_cursor_drawn_x < ctx.cols)
    blit_cell(s_cursor_drawn_x, s_cursor_drawn_y);
  s_cursor_drawn_x = -1;
  s_cursor_drawn_y = -1;
}

static void cursor_paint(void)
{
  if(ctx.yielded || !ctx.cells)
    return;
  if(!ctx.cursor_visible || !ctx.blink_on)
    return;
  int x = ctx.cx;
  int y = ctx.cy;
  if(x >= ctx.cols)
    x = ctx.cols - 1;
  if(y >= ctx.rows)
    y = ctx.rows - 1;
  if(x < 0 || y < 0)
    return;

  fb_cell_t *cell     = &ctx.cells[(size_t)y * (size_t)ctx.cols + (size_t)x];
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

  s_cursor_drawn_x = x;
  s_cursor_drawn_y = y;
}

/** Erase + paint in one call. Cheap when the cursor hasn't moved. */
static void cursor_refresh(void)
{
  cursor_erase();
  cursor_paint();
}

/* Rows that need to be scrolled out at the next flush. Updated by scroll_one
 * during a write; consumed by flush_pending_scroll at end-of-write. Batching
 * matters because pixel writes hit MMIO — collapsing N scrolls into one move
 * keeps `ls` of a long dir interactive instead of bandwidth-bound. */
static int s_pending_scroll = 0;

/* Scrollback ring ------------------------------------------------------- */
static fb_cell_t *s_sb_buf  = NULL; /* kmalloc'd: SCROLLBACK_ROWS * cols    */
static int        s_sb_cols = 0;    /* ctx.cols when buf was allocated       */
static int        s_sb_head = 0;    /* ring head (oldest row index)          */
static int        s_sb_used = 0;    /* rows currently stored                 */
static int        s_sb_view = 0;    /* rows scrolled back; 0 = live view     */

/* Logical scroll only — moves the cell array up by one row and queues a
 * pixel move for the next flush. Cell blits done between now and flush land
 * at their final logical row, so the visible result is the same once the
 * pending pixel move catches up. */
static void scroll_one(void)
{
  size_t row_bytes = (size_t)ctx.cols * sizeof(fb_cell_t);

  /* Save the row scrolling off the top into the scrollback ring. */
  if(s_sb_buf && s_sb_cols == ctx.cols) {
    int slot;
    if(s_sb_used < SCROLLBACK_ROWS) {
      slot = (s_sb_head + s_sb_used) % SCROLLBACK_ROWS;
      s_sb_used++;
    } else {
      slot       = s_sb_head;
      s_sb_head  = (s_sb_head + 1) % SCROLLBACK_ROWS;
    }
    kmemcpy(&s_sb_buf[(size_t)slot * (size_t)ctx.cols], &ctx.cells[0], row_bytes);
  }

  for(int r = 0; r < ctx.rows - 1; r++)
    kmemcpy(
        &ctx.cells[(size_t)r * (size_t)ctx.cols],
        &ctx.cells[(size_t)(r + 1) * (size_t)ctx.cols], row_bytes
    );
  for(int c = 0; c < ctx.cols; c++) {
    fb_cell_t *cell =
        &ctx.cells[(size_t)(ctx.rows - 1) * (size_t)ctx.cols + (size_t)c];
    cell->cp    = (u32)' ';
    cell->fg    = ctx.cur_fg;
    cell->bg    = ctx.cur_bg;
    cell->attr  = 0;
    cell->dirty = 0;
  }
  s_pending_scroll++;
  s_cursor_drawn_x = -1;
  s_cursor_drawn_y = -1;
  /* After a scroll, dirty cells may have shifted rows — expand the range
   * to cover all rows so flush_batch() doesn't miss any. */
  if(ctx.in_batch) {
    ctx.batch_r0 = 0;
    ctx.batch_r1 = ctx.rows - 1;
  }
}

/* Repaint the full visible grid from scrollback + live cells. Called when
 * entering scrollback mode or scrolling within it. */
static void scrollback_repaint(void)
{
  if(!ctx.base || !ctx.cells) return;
  cursor_erase();
  for(int r = 0; r < ctx.rows; r++) {
    const fb_cell_t *src;
    fb_cell_t        blank;
    if(r < s_sb_view) {
      int sb_idx = s_sb_used - s_sb_view + r;
      if(sb_idx < 0 || !s_sb_buf) {
        kmemset(&blank, 0, sizeof blank);
        blank.cp = ' ';
        blank.fg = ctx.default_fg;
        blank.bg = ctx.default_bg;
        src = &blank;
      } else {
        int slot = (s_sb_head + sb_idx) % SCROLLBACK_ROWS;
        src = &s_sb_buf[(size_t)slot * (size_t)ctx.cols];
      }
      for(int c = 0; c < ctx.cols; c++)
        blit_cell_data(src + c, c, r);
    } else {
      src = &ctx.cells[(size_t)(r - s_sb_view) * (size_t)ctx.cols];
      for(int c = 0; c < ctx.cols; c++)
        blit_cell_data(src + c, c, r);
    }
  }
}

static void scrollback_exit(void)
{
  if(s_sb_view == 0) return;
  s_sb_view = 0;
  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      ctx.cells[(size_t)r * (size_t)ctx.cols + (size_t)c].dirty = 1;
}

void fb_console_scrollback_up(int lines)
{
  if(!s_sb_buf || s_sb_used == 0 || lines <= 0) return;
  s_sb_view += lines;
  if(s_sb_view > s_sb_used) s_sb_view = s_sb_used;
  scrollback_repaint();
}

void fb_console_scrollback_down(int lines)
{
  if(s_sb_view == 0 || lines <= 0) return;
  s_sb_view -= lines;
  if(s_sb_view < 0) s_sb_view = 0;
  if(s_sb_view == 0)
    scrollback_exit();
  else
    scrollback_repaint();
}

static void flush_pending_scroll(void)
{
  if(s_pending_scroll <= 0)
    return;

  int n = s_pending_scroll;
  s_pending_scroll = 0;
  if(n > ctx.rows) n = ctx.rows;

  if(ctx.base && ctx.bytes_pp == 4) {
    u32 scroll_px = (u32)n * (u32)ctx.cell_h;
    u32 total_px  = (u32)ctx.rows * (u32)ctx.cell_h;
    u32 copy_px   = total_px - scroll_px;
    if(copy_px > 0) {
      u8 *dst = (u8 *)ctx.base + (u64)ctx.margin_y * ctx.pitch;
      u8 *src = dst + (u64)scroll_px * ctx.pitch;
      kmemcpy(dst, src, (u64)copy_px * ctx.pitch);
    }
    int first_new = ctx.rows - n;
    for(int r = first_new; r < ctx.rows; r++)
      for(int c = 0; c < ctx.cols; c++)
        ctx.cells[(size_t)r * (size_t)ctx.cols + (size_t)c].dirty = 1;
    if(ctx.batch_r0 > first_new) ctx.batch_r0 = first_new;
    if(ctx.batch_r1 < ctx.rows - 1) ctx.batch_r1 = ctx.rows - 1;
    return;
  }

  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      ctx.cells[(size_t)r * (size_t)ctx.cols + (size_t)c].dirty = 1;
  ctx.batch_r0 = 0;
  ctx.batch_r1 = ctx.rows - 1;
}

struct flush_cell_cache {
  const u8 *glyph_base; /* NULL = bg-only (space / blink-off) */
  u32       bg_pk;
  u32       fg_pk;
  u32       fg_r, fg_g, fg_b;
  u32       bg_r, bg_g, bg_b;
  bool      active;
  bool      underline;
};

static void flush_batch(void)
{
  if(!ctx.cells || ctx.batch_r0 > ctx.batch_r1)
    return;
  int r0 = ctx.batch_r0 < 0         ? 0           : ctx.batch_r0;
  int r1 = ctx.batch_r1 >= ctx.rows ? ctx.rows - 1 : ctx.batch_r1;

  if(!ctx.base || ctx.bytes_pp != 4) {
    for(int r = r0; r <= r1; r++)
      for(int c = 0; c < ctx.cols; c++) {
        fb_cell_t *cell = &ctx.cells[(size_t)r * (size_t)ctx.cols + (size_t)c];
        if(cell->dirty) { cell->dirty = 0; blit_cell(c, r); }
      }
    return;
  }

  u32 atlas_bypp = (ctx.atlas_bpp + 7u) / 8u;
  u32 acw = (ctx.atlas_cell_w < (u32)ctx.cell_w) ? ctx.atlas_cell_w : (u32)ctx.cell_w;

  struct flush_cell_cache ci[160];

  for(int cr = r0; cr <= r1; cr++) {
    fb_cell_t *row = &ctx.cells[(size_t)cr * (size_t)ctx.cols];
    u32        py0 = (u32)ctx.margin_y + (u32)cr * (u32)ctx.cell_h;

    for(int cc = 0; cc < ctx.cols; cc++) {
      const fb_cell_t *c = &row[cc];
      ci[cc].active = (c->dirty != 0);
      if(!ci[cc].active) continue;

      u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
      u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

      ci[cc].bg_pk     = 0xFF000000u | eff_bg;
      ci[cc].fg_pk     = 0xFF000000u | eff_fg; /* always set — underline needs it */
      ci[cc].underline = (c->attr & FB_ATTR_UNDERLINE) != 0;

      bool bg_only = (!ctx.atlas_active || c->cp == ' '
                      || ((c->attr & FB_ATTR_BLINK) && !ctx.cell_blink_on));
      if(!bg_only) {
        u32 idx = atlas_lookup_attr(c->cp, c->attr);
        if(idx == ATLAS_NO_GLYPH || idx >= ctx.atlas_n_glyphs) {
          bg_only = true;
        } else {
          ci[cc].fg_pk      = 0xFF000000u | eff_fg;
          ci[cc].fg_r       = (eff_fg >> 16) & 0xffu;
          ci[cc].fg_g       = (eff_fg >>  8) & 0xffu;
          ci[cc].fg_b       =  eff_fg        & 0xffu;
          ci[cc].bg_r       = (eff_bg >> 16) & 0xffu;
          ci[cc].bg_g       = (eff_bg >>  8) & 0xffu;
          ci[cc].bg_b       =  eff_bg        & 0xffu;
          ci[cc].glyph_base = ctx.atlas_pixels
                            + (size_t)idx * (size_t)ctx.atlas_cell_h
                              * (size_t)ctx.atlas_stride;
        }
      }
      if(bg_only) ci[cc].glyph_base = NULL;
    }

    for(u32 spy = 0; spy < (u32)ctx.cell_h; spy++) {
      volatile u32 *fb_line = (volatile u32 *)(ctx.base
                             + (u64)(py0 + spy) * ctx.pitch
                             + (u64)ctx.margin_x * 4u);
      for(int cc = 0; cc < ctx.cols; cc++) {
        if(!ci[cc].active) continue;
        volatile u32 *dst = fb_line + (u32)cc * (u32)ctx.cell_w;

        if(ci[cc].underline && spy >= (u32)ctx.cell_h - 2u) {
          fill32(dst, ci[cc].fg_pk, (u32)ctx.cell_w);
          continue;
        }
        if(!ci[cc].glyph_base || spy >= ctx.atlas_cell_h) {
          fill32(dst, ci[cc].bg_pk, (u32)ctx.cell_w);
          continue;
        }

        const u8 *src = ci[cc].glyph_base + (size_t)spy * (size_t)ctx.atlas_stride;
        blend_glyph_row(dst, src, acw, atlas_bypp,
                        ci[cc].fg_r, ci[cc].fg_g, ci[cc].fg_b,
                        ci[cc].bg_r, ci[cc].bg_g, ci[cc].bg_b,
                        ci[cc].fg_pk, ci[cc].bg_pk);
      }
    }
    for(int cc = 0; cc < ctx.cols; cc++) row[cc].dirty = 0;
  }
}

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
  /* Skip when nothing actually changed — avoids redundant VRAM writes during
   * line-editor redraws that repaint identical content. */
  if(c->cp != cp || c->fg != ctx.cur_fg || c->bg != ctx.cur_bg || c->attr != ctx.cur_attr) {
    c->cp   = cp;
    c->fg   = ctx.cur_fg;
    c->bg   = ctx.cur_bg;
    c->attr = (u16)ctx.cur_attr;
    if(ctx.in_batch) {
      c->dirty = 1;
      if(ctx.cy < ctx.batch_r0) ctx.batch_r0 = ctx.cy;
      if(ctx.cy > ctx.batch_r1) ctx.batch_r1 = ctx.cy;
    } else {
      blit_cell(ctx.cx, ctx.cy);
    }
  }
  ctx.last_cp = cp;
  ctx.cx++;
}

/* Catppuccin Mocha 16-color palette.  Indices match ANSI SGR 30-37 / 90-97. */
static const u32 ansi16_fg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};
static const u32 ansi16_fg_bright[8] = {
    0x585b70u, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xcba6f7u, 0x89dcebu, 0xa6adc8u,
};
static const u32 ansi16_bg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};

/* xterm-256 → RGB. ANSI 16..231 are a 6×6×6 cube; 232..255 are 24 grays. */
static u32 ansi256_to_rgb(unsigned idx)
{
  if(idx < 8u)
    return ansi16_fg[idx];
  if(idx < 16u)
    return ansi16_fg_bright[idx - 8u];
  if(idx < 232u) {
    unsigned i   = idx - 16u;
    unsigned r6  = i / 36u;
    unsigned rem = i % 36u;
    unsigned g6  = rem / 6u;
    unsigned b6  = rem % 6u;
    u32      r   = (r6 == 0u) ? 0u : (55u + 40u * (r6 - 1u));
    u32      g   = (g6 == 0u) ? 0u : (55u + 40u * (g6 - 1u));
    u32      b   = (b6 == 0u) ? 0u : (55u + 40u * (b6 - 1u));
    return (r << 16) | (g << 8) | b;
  }
  u32 v = 8u + 10u * (idx - 232u);
  return (v << 16) | (v << 8) | v;
}

/* Active while ESC ( 0 is in effect. Maps the printable ASCII range used by
 * DEC ACS to Unicode box-drawing / math glyphs. The kernel's CP437 bitmap
 * lacks most of these; the userspace atlas will provide proper glyphs. */
static u32 acs_to_unicode(u8 b)
{
  switch(b) {
  case '`':
    return 0x25C6u; /* ◆ */
  case 'a':
    return 0x2592u; /* ▒ */
  case 'f':
    return 0x00B0u; /* ° */
  case 'g':
    return 0x00B1u; /* ± */
  case 'j':
    return 0x2518u; /* ┘ */
  case 'k':
    return 0x2510u; /* ┐ */
  case 'l':
    return 0x250Cu; /* ┌ */
  case 'm':
    return 0x2514u; /* └ */
  case 'n':
    return 0x253Cu; /* ┼ */
  case 'q':
    return 0x2500u; /* ─ */
  case 't':
    return 0x251Cu; /* ├ */
  case 'u':
    return 0x2524u; /* ┤ */
  case 'v':
    return 0x2534u; /* ┴ */
  case 'w':
    return 0x252Cu; /* ┬ */
  case 'x':
    return 0x2502u; /* │ */
  case 'y':
    return 0x2264u; /* ≤ */
  case 'z':
    return 0x2265u; /* ≥ */
  case '|':
    return 0x2260u; /* ≠ */
  case '~':
    return 0x00B7u; /* · */
  default:
    return (u32)b;
  }
}

/* Parse N decimal params separated by `;` from esc_buf (everything before the
 * final byte). Empty fields default to 0. Returns count parsed. */
static int csi_params(int *pv, int maxn)
{
  int pn = ctx.esc_len - 1;
  int np = 0;
  int i  = 0;
  while(i < pn && np < maxn) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < pn && ctx.esc_buf[i] >= '0' && ctx.esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(ctx.esc_buf[i] - '0');
      i++;
      dig++;
    }
    pv[np++] = (int)((dig != 0) ? acc : 0u);
    if(i < pn && ctx.esc_buf[i] == ';')
      i++;
  }
  return np;
}

static int csi_param1(void)
{
  int pv[8];
  int np = csi_params(pv, 8);
  if(np == 0 || pv[0] == 0)
    return 1;
  return pv[0];
}

static void erase_rect(int y0, int x0, int y1, int x1)
{
  for(int y = y0; y <= y1; y++) {
    if(y < 0 || y >= ctx.rows)
      continue;
    for(int x = x0; x <= x1; x++) {
      if(x < 0 || x >= ctx.cols)
        continue;
      fb_cell_t *c = &ctx.cells[(size_t)y * (size_t)ctx.cols + (size_t)x];
      if(c->cp == (u32)' ' && c->fg == ctx.cur_fg && c->bg == ctx.cur_bg && c->attr == 0)
        continue;
      c->cp   = (u32)' ';
      c->fg   = ctx.cur_fg;
      c->bg   = ctx.cur_bg;
      c->attr = 0;
      if(ctx.in_batch) {
        c->dirty = 1;
        if(y < ctx.batch_r0) ctx.batch_r0 = y;
        if(y > ctx.batch_r1) ctx.batch_r1 = y;
      } else {
        blit_cell(x, y);
      }
    }
  }
}

static void csi_cup(void)
{
  int pv[4];
  int np  = csi_params(pv, 4);
  int row = (np >= 1 && pv[0] >= 1) ? pv[0] : 1;
  int col = (np >= 2 && pv[1] >= 1) ? pv[1] : 1;
  if(row > ctx.rows)
    row = ctx.rows;
  if(col > ctx.cols)
    col = ctx.cols;
  ctx.cy = row - 1;
  ctx.cx = col - 1;
}

static void csi_sgr(void)
{
  int pv[32];
  int np = csi_params(pv, 32);
  if(np == 0) {
    ctx.cur_fg   = ctx.default_fg;
    ctx.cur_bg   = ctx.default_bg;
    ctx.cur_attr = 0;
    return;
  }
  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    if(p == 0) {
      ctx.cur_fg   = ctx.default_fg;
      ctx.cur_bg   = ctx.default_bg;
      ctx.cur_attr = 0;
    } else if(p == 1) {
      ctx.cur_attr |= (u8)FB_ATTR_BOLD;
    } else if(p == 3) {
      ctx.cur_attr |= (u8)FB_ATTR_ITALIC;
    } else if(p == 4) {
      ctx.cur_attr |= (u8)FB_ATTR_UNDERLINE;
    } else if(p == 5) {
      ctx.cur_attr |= (u8)FB_ATTR_BLINK;
    } else if(p == 7) {
      ctx.cur_attr |= (u8)FB_ATTR_REVERSE;
    } else if(p == 22) {
      ctx.cur_attr = (u8)(ctx.cur_attr & ~(u8)FB_ATTR_BOLD);
    } else if(p == 23) {
      ctx.cur_attr = (u8)(ctx.cur_attr & ~(u8)FB_ATTR_ITALIC);
    } else if(p == 24) {
      ctx.cur_attr = (u8)(ctx.cur_attr & ~(u8)FB_ATTR_UNDERLINE);
    } else if(p == 25) {
      ctx.cur_attr = (u8)(ctx.cur_attr & ~(u8)FB_ATTR_BLINK);
    } else if(p == 27) {
      ctx.cur_attr = (u8)(ctx.cur_attr & ~(u8)FB_ATTR_REVERSE);
    } else if(p == 39) {
      ctx.cur_fg = ctx.default_fg;
    } else if(p == 49) {
      ctx.cur_bg = ctx.default_bg;
    } else if(p >= 30 && p <= 37) {
      ctx.cur_fg = ansi16_fg[p - 30];
    } else if(p >= 90 && p <= 97) {
      ctx.cur_fg = ansi16_fg_bright[p - 90];
    } else if(p >= 40 && p <= 47) {
      ctx.cur_bg = ansi16_bg[p - 40];
    } else if(p >= 100 && p <= 107) {
      ctx.cur_bg = ansi16_bg[p - 100];
    } else if(p == 38 && pi + 2 < np && pv[pi + 1] == 5) {
      ctx.cur_fg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == 38 && pi + 4 < np && pv[pi + 1] == 2) {
      u32 r      = (u32)(pv[pi + 2] & 255);
      u32 g      = (u32)(pv[pi + 3] & 255);
      u32 b      = (u32)(pv[pi + 4] & 255);
      ctx.cur_fg = (r << 16) | (g << 8) | b;
      pi += 4;
    } else if(p == 48 && pi + 2 < np && pv[pi + 1] == 5) {
      ctx.cur_bg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == 48 && pi + 4 < np && pv[pi + 1] == 2) {
      u32 r      = (u32)(pv[pi + 2] & 255);
      u32 g      = (u32)(pv[pi + 3] & 255);
      u32 b      = (u32)(pv[pi + 4] & 255);
      ctx.cur_bg = (r << 16) | (g << 8) | b;
      pi += 4;
    }
  }
}

static void csi_dec_private(char cmd)
{
  /* esc_buf starts with `?`. Parse the trailing param list. */
  if(ctx.esc_len < 3)
    return;
  int pv[4];
  int np = 0;
  int i  = 1;
  while(i < ctx.esc_len - 1 && np < 4) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < ctx.esc_len - 1 && ctx.esc_buf[i] >= '0' && ctx.esc_buf[i] <= '9'
    ) {
      acc = acc * 10u + (unsigned)(ctx.esc_buf[i] - '0');
      i++;
      dig++;
    }
    if(dig)
      pv[np++] = (int)acc;
    if(i < ctx.esc_len - 1 && ctx.esc_buf[i] == ';')
      i++;
    else if(!dig && i < ctx.esc_len - 1)
      i++;
  }
  int on = (cmd == 'h');
  for(int k = 0; k < np; k++) {
    if(pv[k] == 25)
      ctx.cursor_visible = (u8)on;
    else if(pv[k] == 1)
      ctx.app_cursor_keys = (on != 0);
    /* ?1049 (alt screen) intentionally ignored — draw into the live grid. */
  }
}

static void handle_csi(void)
{
  if(ctx.esc_len < 1)
    return;
  char cmd = ctx.esc_buf[ctx.esc_len - 1];

  if((cmd == 'h' || cmd == 'l') && ctx.esc_len >= 2 && ctx.esc_buf[0] == '?') {
    csi_dec_private(cmd);
    return;
  }

  switch(cmd) {
  case 'A': { /* CUU */
    int n  = csi_param1();
    ctx.cy = (ctx.cy >= n) ? (ctx.cy - n) : 0;
    break;
  }
  case 'B': { /* CUD */
    int n  = csi_param1();
    int mx = ctx.rows - 1 - ctx.cy;
    if(n > mx)
      n = mx;
    if(n > 0)
      ctx.cy += n;
    break;
  }
  case 'C': { /* CUF */
    int n  = csi_param1();
    int mx = ctx.cols - 1 - ctx.cx;
    if(n > mx)
      n = mx;
    if(n > 0)
      ctx.cx += n;
    break;
  }
  case 'D': { /* CUB */
    int n  = csi_param1();
    ctx.cx = (ctx.cx >= n) ? (ctx.cx - n) : 0;
    break;
  }
  case 'H':
  case 'f':
    csi_cup();
    break;
  case 'G': { /* CHA — absolute column (1-based) */
    int n = csi_param1();
    if(n > ctx.cols)
      n = ctx.cols;
    ctx.cx = n - 1;
    break;
  }
  case 'd': { /* VPA — absolute row (1-based) */
    int n = csi_param1();
    if(n > ctx.rows)
      n = ctx.rows;
    ctx.cy = n - 1;
    break;
  }
  case 'J': { /* ED */
    int pv[2];
    int np   = csi_params(pv, 2);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 2) {
      erase_rect(0, 0, ctx.rows - 1, ctx.cols - 1);
    } else if(mode == 0) {
      erase_rect(ctx.cy, ctx.cx, ctx.cy, ctx.cols - 1);
      if(ctx.cy < ctx.rows - 1)
        erase_rect(ctx.cy + 1, 0, ctx.rows - 1, ctx.cols - 1);
    } else if(mode == 1) {
      if(ctx.cy > 0)
        erase_rect(0, 0, ctx.cy - 1, ctx.cols - 1);
      erase_rect(ctx.cy, 0, ctx.cy, ctx.cx);
    }
    break;
  }
  case 'K': { /* EL */
    int pv[2];
    int np   = csi_params(pv, 2);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 0)
      erase_rect(ctx.cy, ctx.cx, ctx.cy, ctx.cols - 1);
    else if(mode == 1)
      erase_rect(ctx.cy, 0, ctx.cy, ctx.cx);
    else if(mode == 2)
      erase_rect(ctx.cy, 0, ctx.cy, ctx.cols - 1);
    break;
  }
  case 'X': { /* ECH — erase Pn chars at cursor (no cursor move) */
    int n  = csi_param1();
    int x1 = ctx.cx + n - 1;
    if(x1 >= ctx.cols)
      x1 = ctx.cols - 1;
    erase_rect(ctx.cy, ctx.cx, ctx.cy, x1);
    break;
  }
  case 'm':
    csi_sgr();
    break;
  case 'b': { /* REP — repeat last codepoint Pn times */
    if(ctx.last_cp == 0)
      break;
    int n = csi_param1();
    for(int i = 0; i < n; i++)
      put_cp_at_cursor(ctx.last_cp);
    break;
  }
  case 's':
    ctx.saved_cx = ctx.cx;
    ctx.saved_cy = ctx.cy;
    break;
  case 'u':
    ctx.cx = ctx.saved_cx;
    ctx.cy = ctx.saved_cy;
    break;
  default:
    break;
  }
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
 * completes; ANSI/CSI sequences are picked off ahead of this by feed_byte
 * before they ever reach feed_utf8. */
static void feed_utf8(u8 b)
{
  if(ctx.utf8_rem == 0) {
    if(b < 0x20u || b == 0x7fu) {
      handle_control(b);
      return;
    }
    if(b < 0x80u) {
      /* G0 DEC ACS in effect: map printable ASCII to box-drawing/math. */
      if(ctx.g0_acs && b >= 0x60u && b <= 0x7eu) {
        put_cp_at_cursor(acs_to_unicode(b));
        return;
      }
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

bool fb_console_init(void *fb, u64 width, u64 height, u64 pitch, u16 bpp)
{
  ctx.base     = (volatile u8 *)fb;
  ctx.width    = width;
  ctx.height   = height;
  ctx.pitch    = pitch;
  ctx.bytes_pp = bytes_pp_from_bpp(bpp);
  ctx.cell_w   = FONT_W;
  ctx.cell_h   = FONT_H;
  ctx.margin_x = 0;
  ctx.margin_y = 0;
  ctx.cols     = (int)(width / (u64)ctx.cell_w);
  ctx.rows     = (int)(height / (u64)ctx.cell_h);
  ctx.default_fg = 0xcdd6f4u; /* Catppuccin Mocha Text */
  ctx.default_bg = 0x1e1e2eu; /* Catppuccin Mocha Base */
  ctx.cur_fg     = ctx.default_fg;
  ctx.cur_bg     = ctx.default_bg;
  ctx.cx = ctx.cy    = 0;
  ctx.utf8_rem       = 0;
  ctx.blink_ticks    = 50;
  ctx.blink_on       = 1;
  ctx.cell_blink_on  = 1;
  ctx.cur_attr       = 0;
  ctx.cursor_visible = 1;
  ctx.yielded        = false;
  ctx.in_head = ctx.in_tail = 0;

  size_t total = (size_t)ctx.rows * (size_t)ctx.cols;
  ctx.cells    = (fb_cell_t *)kmalloc(total * sizeof(fb_cell_t));
  if(!ctx.cells)
    return false;
  for(size_t i = 0; i < total; i++) {
    ctx.cells[i].cp    = (u32)' ';
    ctx.cells[i].fg    = ctx.default_fg;
    ctx.cells[i].bg    = ctx.default_bg;
    ctx.cells[i].attr  = 0;
    ctx.cells[i].dirty = 0;
  }

  s_sb_buf  = (fb_cell_t *)kmalloc((size_t)SCROLLBACK_ROWS * (size_t)ctx.cols * sizeof(fb_cell_t));
  s_sb_cols = ctx.cols;
  s_sb_head = s_sb_used = s_sb_view = 0;
  return true;
}

/* ESC state machine: drives the byte stream through ANSI / CSI / charset
 * states before falling through to UTF-8 → cell emission. */
static void feed_byte(u8 b)
{
  switch(ctx.esc_state) {
  case 1: /* after ESC */
    if(b == '[') {
      ctx.esc_state = 2;
      ctx.esc_len   = 0;
      return;
    }
    if(b == '7') { /* DECSC */
      ctx.saved_cx  = ctx.cx;
      ctx.saved_cy  = ctx.cy;
      ctx.esc_state = 0;
      return;
    }
    if(b == '8') { /* DECRC */
      ctx.cx        = ctx.saved_cx;
      ctx.cy        = ctx.saved_cy;
      ctx.esc_state = 0;
      return;
    }
    if(b == '(' || b == ')') {
      ctx.esc_state = 3; /* wait for designator byte */
      return;
    }
    /* Unrecognised ESC <byte> — swallow (DECKPAM/DECKPNM = / >, etc.). */
    ctx.esc_state = 0;
    return;
  case 2: /* inside CSI */
    if((b >= '0' && b <= '9') || b == ';' || b == '?') {
      if(ctx.esc_len < (u8)(sizeof ctx.esc_buf - 1))
        ctx.esc_buf[ctx.esc_len++] = (char)b;
      return;
    }
    if(ctx.esc_len < (u8)(sizeof ctx.esc_buf - 1))
      ctx.esc_buf[ctx.esc_len++] = (char)b;
    handle_csi();
    ctx.esc_state = 0;
    return;
  case 3: /* charset designator */
    if(b == '0')
      ctx.g0_acs = 1;
    else if(b == 'B' || b == 'A' || b == 'U' || b == '1' || b == '2')
      ctx.g0_acs = 0;
    ctx.esc_state = 0;
    return;
  default:
    break;
  }

  if(b == 0x1bu) {
    ctx.esc_state = 1;
    ctx.utf8_rem  = 0;
    return;
  }
  feed_utf8(b);
}

void fb_console_write_begin(void)
{
  if(ctx.yielded || !ctx.cells)
    return;
  scrollback_exit(); /* any write returns to live view */
  ctx.batch_r0 = ctx.rows;
  ctx.batch_r1 = -1;
  if(s_cursor_drawn_x >= 0 && s_cursor_drawn_y >= 0) {
    fb_cell_t *cc = &ctx.cells[
        (size_t)s_cursor_drawn_y * (size_t)ctx.cols + (size_t)s_cursor_drawn_x];
    cc->dirty = 1;
    if(s_cursor_drawn_y < ctx.batch_r0) ctx.batch_r0 = s_cursor_drawn_y;
    if(s_cursor_drawn_y > ctx.batch_r1) ctx.batch_r1 = s_cursor_drawn_y;
    s_cursor_drawn_x = -1;
    s_cursor_drawn_y = -1;
  }
  ctx.in_batch = true;
}

void fb_console_write_raw(const void *buf, size_t len)
{
  if(ctx.yielded || !ctx.cells)
    return;
  const u8 *p = (const u8 *)buf;
  for(size_t i = 0; i < len; i++)
    feed_byte(p[i]);
}

void fb_console_write_end(void)
{
  if(ctx.yielded || !ctx.cells)
    return;
  ctx.in_batch = false;
  flush_pending_scroll();
  flush_batch();
  ctx.blink_ticks = 50;
  ctx.blink_on    = 1;
  cursor_paint();
}

void fb_console_write(const void *buf, size_t len)
{
  fb_console_write_begin();
  fb_console_write_raw(buf, len);
  fb_console_write_end();
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
    ctx.blink_on      = (u8)!ctx.blink_on;
    ctx.cell_blink_on = ctx.blink_on;
    ctx.blink_ticks   = 50;

    ctx.batch_r0 = ctx.rows;
    ctx.batch_r1 = -1;
    for(int r = 0; r < ctx.rows; r++) {
      for(int c = 0; c < ctx.cols; c++) {
        fb_cell_t *cell = &ctx.cells[(size_t)r * (size_t)ctx.cols + (size_t)c];
        if(cell->attr & FB_ATTR_BLINK) {
          cell->dirty = 1;
          if(r < ctx.batch_r0) ctx.batch_r0 = r;
          if(r > ctx.batch_r1) ctx.batch_r1 = r;
        }
      }
    }
    if(ctx.batch_r0 <= ctx.batch_r1)
      flush_batch();

    cursor_refresh();
  } else {
    ctx.blink_ticks--;
  }
}

/* Caps are generous — they bound damage from a buggy shim, not enforce policy. */
static bool atlas_meta_is_sane(const fb_console_atlas_t *meta)
{
  if(meta->cell_w == 0u || meta->cell_h == 0u || meta->cell_w > 64u ||
     meta->cell_h > 64u)
    return false;
  if(meta->n_glyphs == 0u || meta->n_glyphs > 16384u)
    return false;
  if(meta->n_cp == 0u || meta->n_cp > 0x4000u)
    return false;
  if(meta->pixels_size == 0u || meta->pixels_size > 16u * 1024u * 1024u)
    return false;
  /* Stride: one row of pixels must fit inside cell_w * max-bytes-per-pixel.
   * Cap bypp at 4 (the widest fb format we render); 1-byte alpha is typical. */
  if(meta->stride_bytes == 0u || meta->stride_bytes > 4u * meta->cell_w)
    return false;
  return true;
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
  if(ctx.atlas_pixels)
    kfree(ctx.atlas_pixels);
  if(ctx.atlas_cp_map)
    kfree(ctx.atlas_cp_map);

  ctx.atlas_pixels   = new_pixels;
  ctx.atlas_cp_map   = new_cp_map;
  ctx.atlas_cell_w   = meta->cell_w;
  ctx.atlas_cell_h   = meta->cell_h;
  ctx.atlas_stride   = meta->stride_bytes;
  ctx.atlas_bpp      = meta->bpp;
  ctx.atlas_n_glyphs = meta->n_glyphs;
  ctx.atlas_n_cp     = meta->n_cp;
  ctx.atlas_fallback    = meta->fallback_idx;
  ctx.atlas_bold_base   = meta->bold_offset;
  ctx.atlas_italic_base = meta->italic_offset;
  ctx.atlas_active      = true;

  /* Adopt the atlas's cell pixel size and reflow the grid. Cursor + saved
   * cursor get clamped into the new geometry; existing content is discarded
   * (cleared to a fresh grid) because cell coordinates don't survive a
   * cols/rows change in any well-defined way. */
  int new_cell_w = (int)meta->cell_w;
  int new_cell_h = (int)meta->cell_h;
  int new_marg_x = FB_CONSOLE_MARGIN;
  int new_marg_y = FB_CONSOLE_MARGIN;
  int old_cols   = ctx.cols;
  int old_rows   = ctx.rows;
  if(new_cell_w != ctx.cell_w || new_cell_h != ctx.cell_h ||
     new_marg_x != ctx.margin_x || new_marg_y != ctx.margin_y) {
    int new_cols = (int)((ctx.width - 2u * (u64)new_marg_x) / (u64)new_cell_w);
    int new_rows = (int)((ctx.height - 2u * (u64)new_marg_y) / (u64)new_cell_h);
    if(new_cols < 1)
      new_cols = 1;
    if(new_rows < 1)
      new_rows = 1;
    size_t     total = (size_t)new_cols * (size_t)new_rows;
    fb_cell_t *nc    = (fb_cell_t *)kmalloc(total * sizeof(fb_cell_t));
    if(nc) {
      for(size_t i = 0; i < total; i++) {
        nc[i].cp   = (u32)' ';
        nc[i].fg   = ctx.default_fg;
        nc[i].bg   = ctx.default_bg;
        nc[i].attr = 0;
      }
      if(ctx.cells)
        kfree(ctx.cells);
      ctx.cells    = nc;
      ctx.cols     = new_cols;
      ctx.rows     = new_rows;
      ctx.cell_w   = new_cell_w;
      ctx.cell_h   = new_cell_h;
      ctx.margin_x = new_marg_x;
      ctx.margin_y = new_marg_y;
      ctx.cx = ctx.cy = 0;
      ctx.saved_cx = ctx.saved_cy = 0;
      /* Reallocate scrollback for the new column count. */
      if(s_sb_buf) kfree(s_sb_buf);
      s_sb_buf  = (fb_cell_t *)kmalloc((size_t)SCROLLBACK_ROWS * (size_t)new_cols * sizeof(fb_cell_t));
      s_sb_cols = new_cols;
      s_sb_head = s_sb_used = s_sb_view = 0;
      /* Wipe stale pixels left around the old grid. */
      for(u32 y = 0; y < ctx.height; y++)
        for(u32 x = 0; x < ctx.width; x++)
          fb_put_pixel(x, y, ctx.default_bg);
    }
    /* If kmalloc fails, fall through and repaint with the existing grid;
     * the atlas blit will just clip to ctx.cell_w/cell_h as before. */
  }

  /* Repaint the whole grid through the new path. The cursor cell, if any,
   * was overwritten by that loop so its tracked position is now stale; any
   * pending scroll is moot because we just repainted everything. */
  s_pending_scroll = 0;
  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      blit_cell(c, r);
  s_cursor_drawn_x = -1;
  s_cursor_drawn_y = -1;
  cursor_paint();

  /* Wake every TUI so they re-query TIOCGWINSZ and redraw at the real grid
   * size. Skipped when the grid stayed the same (e.g. atlas reloaded with
   * identical metrics) — no point waking anyone in that case. */
  if(ctx.cols != old_cols || ctx.rows != old_rows)
    proc_signal_broadcast(SIGWINCH);
  return 0;
}

void fb_console_get_size(int *cols, int *rows)
{
  if(cols)
    *cols = ctx.cols;
  if(rows)
    *rows = ctx.rows;
}

bool fb_console_app_cursor_keys(void)
{
  return ctx.app_cursor_keys;
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
  s_pending_scroll = 0;
  for(int r = 0; r < ctx.rows; r++)
    for(int c = 0; c < ctx.cols; c++)
      blit_cell(c, r);
  s_cursor_drawn_x = -1;
  s_cursor_drawn_y = -1;
  cursor_paint();
}
