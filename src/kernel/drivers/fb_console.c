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
  ctx.last_cp = cp;
  ctx.cx++;
}

/* ---- ANSI color palettes ---------------------------------------------- */

/* 16-color palette (Nord-ish, matches userspace fb_tty for visual continuity).
 */
static const u32 ansi16_fg[8] = {
    0x3b4252u, 0xbf616au, 0xa3be8cu, 0xebcb8bu,
    0x5e81acu, 0xb48eadu, 0x88c0d0u, 0xeceff4u,
};
static const u32 ansi16_fg_bright[8] = {
    0x4c566au, 0xd08770u, 0xb8d99bu, 0xf2e9c4u,
    0x81a1c1u, 0xc89fc8u, 0x8fbcbbu, 0xe5e9f0u,
};
static const u32 ansi16_bg[8] = {
    0x2e3440u, 0x6a4a4eu, 0x3b5348u, 0x57564eu,
    0x3d4f66u, 0x403d52u, 0x3b5254u, 0xe5e9f0u,
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

/* ---- DEC Special Graphics → Unicode ----------------------------------- */

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

/* ---- CSI parameter parsing -------------------------------------------- */

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

/* ---- CSI command implementations -------------------------------------- */

static void erase_rect(int y0, int x0, int y1, int x1)
{
  for(int y = y0; y <= y1; y++) {
    if(y < 0 || y >= ctx.rows)
      continue;
    for(int x = x0; x <= x1; x++) {
      if(x < 0 || x >= ctx.cols)
        continue;
      fb_cell_t *c = &ctx.cells[(size_t)y * (size_t)ctx.cols + (size_t)x];
      c->cp        = (u32)' ';
      c->fg        = ctx.cur_fg;
      c->bg        = ctx.cur_bg;
      c->attr      = 0;
      blit_cell(x, y);
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
    ctx.cur_fg = ctx.default_fg;
    ctx.cur_bg = ctx.default_bg;
    return;
  }
  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    if(p == 0) {
      ctx.cur_fg = ctx.default_fg;
      ctx.cur_bg = ctx.default_bg;
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
    /* bold/dim/underline/blink/reverse parsed-but-ignored — kernel grid can
     * layer them later via the unused attr field. */
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

void fb_console_write(const void *buf, size_t len)
{
  if(ctx.yielded || !ctx.cells)
    return;
  const u8 *p = (const u8 *)buf;
  for(size_t i = 0; i < len; i++)
    feed_byte(p[i]);
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
