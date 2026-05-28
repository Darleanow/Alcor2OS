/**
 * @file src/kernel/drivers/fb_console.c
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
#include <kernel/drivers/fb_console_internal.h>

/* Forward declaration — avoids pulling in proc/signal.h for one call. */
#define SIGWINCH 28
void             proc_signal_broadcast(int signum);

fb_console_ctx_t fb_ctx;

struct flush_cell_cache
{
  const u8 *glyph_base; /* NULL = bg-only (space / blink-off) */
  u32       bg_pk;
  u32       fg_pk;
  u32       fg_r, fg_g, fg_b;
  u32       bg_r, bg_g, bg_b;
  bool      active;
  bool      underline;
};

/** @brief Per-row scratch used by flush_batch. ~48 B per cell — at 160+ cols
 *         far too large for the 8 KiB kernel stack, so heap-owned and grown
 *         on demand whenever the grid widens. */
static struct flush_cell_cache *s_flush_ci      = NULL;
static int                      s_flush_ci_cols = 0;

/**
 * @brief Grow @ref s_flush_ci to hold at least @p cols entries.
 *
 * Called on grid init and whenever a SET_ATLAS reflow widens the grid.
 * Allocation failure leaves the previous buffer in place — flush_batch
 * tolerates @ref s_flush_ci being NULL but not smaller than the current grid,
 * so retaining the larger old buffer is preferable to shrinking on failure.
 *
 * @param cols  Minimum capacity in cells.
 */
static void flush_ci_ensure(int cols)
{
  if(cols <= s_flush_ci_cols && s_flush_ci)
    return;
  struct flush_cell_cache *nb = (struct flush_cell_cache *)kmalloc(
      (size_t)cols * sizeof(struct flush_cell_cache)
  );
  if(!nb)
    return;
  if(s_flush_ci)
    kfree(s_flush_ci);
  s_flush_ci      = nb;
  s_flush_ci_cols = cols;
}

static void flush_batch(void)
{
  if(!fb_ctx.cells || fb_ctx.batch_r0 > fb_ctx.batch_r1)
    return;
  int r0 = fb_ctx.batch_r0 < 0 ? 0 : fb_ctx.batch_r0;
  int r1 = fb_ctx.batch_r1 >= fb_ctx.rows ? fb_ctx.rows - 1 : fb_ctx.batch_r1;

  if(!fb_ctx.base || fb_ctx.bytes_pp != 4) {
    for(int r = r0; r <= r1; r++)
      for(int c = 0; c < fb_ctx.cols; c++) {
        fb_cell_t *cell =
            &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c];
        if(cell->dirty) {
          cell->dirty = 0;
          blit_cell(c, r);
        }
      }
    return;
  }

  u32 atlas_bypp = (fb_ctx.atlas_bpp + 7u) / 8u;
  u32 acw = (fb_ctx.atlas_cell_w < (u32)fb_ctx.cell_w) ? fb_ctx.atlas_cell_w
                                                       : (u32)fb_ctx.cell_w;

  /* Heap-owned scratch; sized at grid init and grown on reflow. The setup
   * loop below fills every column unconditionally, so we don't pre-zero. */
  if(!s_flush_ci || s_flush_ci_cols < fb_ctx.cols)
    flush_ci_ensure(fb_ctx.cols);
  if(!s_flush_ci)
    return;
  struct flush_cell_cache *ci = s_flush_ci;

  for(int cr = r0; cr <= r1; cr++) {
    fb_cell_t *row = &fb_ctx.cells[(size_t)cr * (size_t)fb_ctx.cols];
    u32        py0 = (u32)fb_ctx.margin_y + (u32)cr * (u32)fb_ctx.cell_h;

    for(int cc = 0; cc < fb_ctx.cols; cc++) {
      const fb_cell_t *c = &row[cc];
      ci[cc].active      = (c->dirty != 0);
      if(!ci[cc].active)
        continue;

      u32 eff_fg = (c->attr & FB_ATTR_REVERSE) ? c->bg : c->fg;
      u32 eff_bg = (c->attr & FB_ATTR_REVERSE) ? c->fg : c->bg;

      ci[cc].bg_pk = 0xFF000000u | eff_bg;
      ci[cc].fg_pk = 0xFF000000u | eff_fg; /* always set — underline needs it */
      ci[cc].underline = (c->attr & FB_ATTR_UNDERLINE) != 0;

      bool bg_only =
          (!fb_ctx.atlas_active || c->cp == ' ' ||
           ((c->attr & FB_ATTR_BLINK) && !fb_ctx.cell_blink_on));
      if(!bg_only) {
        u32 idx = atlas_lookup_attr(c->cp, c->attr);
        if(idx == ATLAS_NO_GLYPH || idx >= fb_ctx.atlas_n_glyphs) {
          bg_only = true;
        } else {
          ci[cc].fg_pk = 0xFF000000u | eff_fg;
          ci[cc].fg_r  = (eff_fg >> 16) & 0xffu;
          ci[cc].fg_g  = (eff_fg >> 8) & 0xffu;
          ci[cc].fg_b  = eff_fg & 0xffu;
          ci[cc].bg_r  = (eff_bg >> 16) & 0xffu;
          ci[cc].bg_g  = (eff_bg >> 8) & 0xffu;
          ci[cc].bg_b  = eff_bg & 0xffu;
          ci[cc].glyph_base =
              fb_ctx.atlas_pixels + (size_t)idx * (size_t)fb_ctx.atlas_cell_h *
                                        (size_t)fb_ctx.atlas_stride;
        }
      }
      if(bg_only)
        ci[cc].glyph_base = NULL;
    }

    for(u32 spy = 0; spy < (u32)fb_ctx.cell_h; spy++) {
      volatile u32 *fb_line =
          (volatile u32 *)(fb_ctx.base + (u64)(py0 + spy) * fb_ctx.pitch +
                           (u64)fb_ctx.margin_x * 4u);
      for(int cc = 0; cc < fb_ctx.cols; cc++) {
        if(!ci[cc].active)
          continue;
        volatile u32 *dst = fb_line + (size_t)cc * (size_t)fb_ctx.cell_w;

        if(ci[cc].underline && spy >= (u32)fb_ctx.cell_h - 2u) {
          fill32(dst, ci[cc].fg_pk, (u32)fb_ctx.cell_w);
          continue;
        }
        if(!ci[cc].glyph_base || spy >= fb_ctx.atlas_cell_h) {
          fill32(dst, ci[cc].bg_pk, (u32)fb_ctx.cell_w);
          continue;
        }

        const u8 *src =
            ci[cc].glyph_base + (size_t)spy * (size_t)fb_ctx.atlas_stride;
        blend_glyph_row(
            dst, src, acw, atlas_bypp, ci[cc].fg_r, ci[cc].fg_g, ci[cc].fg_b,
            ci[cc].bg_r, ci[cc].bg_g, ci[cc].bg_b, ci[cc].fg_pk, ci[cc].bg_pk
        );
      }
    }
    for(int cc = 0; cc < fb_ctx.cols; cc++)
      row[cc].dirty = 0;
  }
}

static void put_cp_at_cursor(u32 cp)
{
  if(fb_ctx.cx >= fb_ctx.cols) {
    fb_ctx.cx = 0;
    fb_ctx.cy++;
    if(fb_ctx.cy >= fb_ctx.rows) {
      scroll_one();
      fb_ctx.cy = fb_ctx.rows - 1;
    }
  }
  fb_cell_t *c =
      &fb_ctx
           .cells[(size_t)fb_ctx.cy * (size_t)fb_ctx.cols + (size_t)fb_ctx.cx];
  /* Skip when nothing actually changed — avoids redundant VRAM writes during
   * line-editor redraws that repaint identical content. */
  if(c->cp != cp || c->fg != fb_ctx.cur_fg || c->bg != fb_ctx.cur_bg ||
     c->attr != fb_ctx.cur_attr) {
    c->cp   = cp;
    c->fg   = fb_ctx.cur_fg;
    c->bg   = fb_ctx.cur_bg;
    c->attr = (u16)fb_ctx.cur_attr;
    if(fb_ctx.in_batch) {
      c->dirty = 1;
      if(fb_ctx.cy < fb_ctx.batch_r0)
        fb_ctx.batch_r0 = fb_ctx.cy;
      if(fb_ctx.cy > fb_ctx.batch_r1)
        fb_ctx.batch_r1 = fb_ctx.cy;
    } else {
      blit_cell(fb_ctx.cx, fb_ctx.cy);
    }
  }
  fb_ctx.last_cp = cp;
  fb_ctx.cx++;
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
  int pn = fb_ctx.esc_len - 1;
  int np = 0;
  int i  = 0;
  while(i < pn && np < maxn) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < pn && fb_ctx.esc_buf[i] >= '0' && fb_ctx.esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(fb_ctx.esc_buf[i] - '0');
      i++;
      dig++;
    }
    pv[np++] = (int)((dig != 0) ? acc : 0u);
    if(i < pn && fb_ctx.esc_buf[i] == ';')
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
    if(y < 0 || y >= fb_ctx.rows)
      continue;
    for(int x = x0; x <= x1; x++) {
      if(x < 0 || x >= fb_ctx.cols)
        continue;
      fb_cell_t *c = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols + (size_t)x];
      if(c->cp == (u32)' ' && c->fg == fb_ctx.cur_fg &&
         c->bg == fb_ctx.cur_bg && c->attr == 0)
        continue;
      c->cp   = (u32)' ';
      c->fg   = fb_ctx.cur_fg;
      c->bg   = fb_ctx.cur_bg;
      c->attr = 0;
      if(fb_ctx.in_batch) {
        c->dirty = 1;
        if(y < fb_ctx.batch_r0)
          fb_ctx.batch_r0 = y;
        if(y > fb_ctx.batch_r1)
          fb_ctx.batch_r1 = y;
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
  if(row > fb_ctx.rows)
    row = fb_ctx.rows;
  if(col > fb_ctx.cols)
    col = fb_ctx.cols;
  fb_ctx.cy = row - 1;
  fb_ctx.cx = col - 1;
}

static void csi_sgr(void)
{
  int pv[32];
  int np = csi_params(pv, 32);
  if(np == 0) {
    fb_ctx.cur_fg   = fb_ctx.default_fg;
    fb_ctx.cur_bg   = fb_ctx.default_bg;
    fb_ctx.cur_attr = 0;
    return;
  }
  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    if(p == 0) {
      fb_ctx.cur_fg   = fb_ctx.default_fg;
      fb_ctx.cur_bg   = fb_ctx.default_bg;
      fb_ctx.cur_attr = 0;
    } else if(p == 1) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_BOLD;
    } else if(p == 3) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_ITALIC;
    } else if(p == 4) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_UNDERLINE;
    } else if(p == 5) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_BLINK;
    } else if(p == 7) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_REVERSE;
    } else if(p == 22) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_BOLD);
    } else if(p == 23) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_ITALIC);
    } else if(p == 24) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_UNDERLINE);
    } else if(p == 25) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_BLINK);
    } else if(p == 27) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_REVERSE);
    } else if(p == 39) {
      fb_ctx.cur_fg = fb_ctx.default_fg;
    } else if(p == 49) {
      fb_ctx.cur_bg = fb_ctx.default_bg;
    } else if(p >= 30 && p <= 37) {
      fb_ctx.cur_fg = ansi16_fg[p - 30];
    } else if(p >= 90 && p <= 97) {
      fb_ctx.cur_fg = ansi16_fg_bright[p - 90];
    } else if(p >= 40 && p <= 47) {
      fb_ctx.cur_bg = ansi16_bg[p - 40];
    } else if(p >= 100 && p <= 107) {
      fb_ctx.cur_bg = ansi16_bg[p - 100];
    } else if(p == 38 && pi + 2 < np && pv[pi + 1] == 5) {
      fb_ctx.cur_fg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == 38 && pi + 4 < np && pv[pi + 1] == 2) {
      u32 r         = (u32)(pv[pi + 2] & 255);
      u32 g         = (u32)(pv[pi + 3] & 255);
      u32 b         = (u32)(pv[pi + 4] & 255);
      fb_ctx.cur_fg = (r << 16) | (g << 8) | b;
      pi += 4;
    } else if(p == 48 && pi + 2 < np && pv[pi + 1] == 5) {
      fb_ctx.cur_bg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == 48 && pi + 4 < np && pv[pi + 1] == 2) {
      u32 r         = (u32)(pv[pi + 2] & 255);
      u32 g         = (u32)(pv[pi + 3] & 255);
      u32 b         = (u32)(pv[pi + 4] & 255);
      fb_ctx.cur_bg = (r << 16) | (g << 8) | b;
      pi += 4;
    }
  }
}

static void csi_dec_private(char cmd)
{
  /* esc_buf starts with `?`. Parse the trailing param list. */
  if(fb_ctx.esc_len < 3)
    return;
  int pv[4];
  int np = 0;
  int i  = 1;
  while(i < fb_ctx.esc_len - 1 && np < 4) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < fb_ctx.esc_len - 1 && fb_ctx.esc_buf[i] >= '0' &&
          fb_ctx.esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(fb_ctx.esc_buf[i] - '0');
      i++;
      dig++;
    }
    if(dig)
      pv[np++] = (int)acc;
    if(i < fb_ctx.esc_len - 1 && fb_ctx.esc_buf[i] == ';')
      i++;
    else if(!dig && i < fb_ctx.esc_len - 1)
      i++;
  }
  int on = (cmd == 'h');
  for(int k = 0; k < np; k++) {
    if(pv[k] == 25)
      fb_ctx.cursor_visible = (u8)on;
    else if(pv[k] == 1)
      fb_ctx.app_cursor_keys = (on != 0);
    /* ?1049 (alt screen) intentionally ignored — draw into the live grid. */
  }
}

static void handle_csi(void)
{
  if(fb_ctx.esc_len < 1)
    return;
  char cmd = fb_ctx.esc_buf[fb_ctx.esc_len - 1];

  if((cmd == 'h' || cmd == 'l') && fb_ctx.esc_len >= 2 &&
     fb_ctx.esc_buf[0] == '?') {
    csi_dec_private(cmd);
    return;
  }

  switch(cmd) {
  case 'A': { /* CUU */
    int n     = csi_param1();
    fb_ctx.cy = (fb_ctx.cy >= n) ? (fb_ctx.cy - n) : 0;
    break;
  }
  case 'B': { /* CUD */
    int n  = csi_param1();
    int mx = fb_ctx.rows - 1 - fb_ctx.cy;
    if(n > mx)
      n = mx;
    if(n > 0)
      fb_ctx.cy += n;
    break;
  }
  case 'C': { /* CUF */
    int n  = csi_param1();
    int mx = fb_ctx.cols - 1 - fb_ctx.cx;
    if(n > mx)
      n = mx;
    if(n > 0)
      fb_ctx.cx += n;
    break;
  }
  case 'D': { /* CUB */
    int n     = csi_param1();
    fb_ctx.cx = (fb_ctx.cx >= n) ? (fb_ctx.cx - n) : 0;
    break;
  }
  case 'H':
  case 'f':
    csi_cup();
    break;
  case 'G': { /* CHA — absolute column (1-based) */
    int n = csi_param1();
    if(n > fb_ctx.cols)
      n = fb_ctx.cols;
    fb_ctx.cx = n - 1;
    break;
  }
  case 'd': { /* VPA — absolute row (1-based) */
    int n = csi_param1();
    if(n > fb_ctx.rows)
      n = fb_ctx.rows;
    fb_ctx.cy = n - 1;
    break;
  }
  case 'J': { /* ED */
    int pv[2];
    int np   = csi_params(pv, 2);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 2) {
      erase_rect(0, 0, fb_ctx.rows - 1, fb_ctx.cols - 1);
    } else if(mode == 0) {
      erase_rect(fb_ctx.cy, fb_ctx.cx, fb_ctx.cy, fb_ctx.cols - 1);
      if(fb_ctx.cy < fb_ctx.rows - 1)
        erase_rect(fb_ctx.cy + 1, 0, fb_ctx.rows - 1, fb_ctx.cols - 1);
    } else if(mode == 1) {
      if(fb_ctx.cy > 0)
        erase_rect(0, 0, fb_ctx.cy - 1, fb_ctx.cols - 1);
      erase_rect(fb_ctx.cy, 0, fb_ctx.cy, fb_ctx.cx);
    }
    break;
  }
  case 'K': { /* EL */
    int pv[2];
    int np   = csi_params(pv, 2);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 0)
      erase_rect(fb_ctx.cy, fb_ctx.cx, fb_ctx.cy, fb_ctx.cols - 1);
    else if(mode == 1)
      erase_rect(fb_ctx.cy, 0, fb_ctx.cy, fb_ctx.cx);
    else if(mode == 2)
      erase_rect(fb_ctx.cy, 0, fb_ctx.cy, fb_ctx.cols - 1);
    break;
  }
  case 'X': { /* ECH — erase Pn chars at cursor (no cursor move) */
    int n  = csi_param1();
    int x1 = fb_ctx.cx + n - 1;
    if(x1 >= fb_ctx.cols)
      x1 = fb_ctx.cols - 1;
    erase_rect(fb_ctx.cy, fb_ctx.cx, fb_ctx.cy, x1);
    break;
  }
  case 'm':
    csi_sgr();
    break;
  case 'b': { /* REP — repeat last codepoint Pn times */
    if(fb_ctx.last_cp == 0)
      break;
    int n = csi_param1();
    for(int i = 0; i < n; i++)
      put_cp_at_cursor(fb_ctx.last_cp);
    break;
  }
  case 's':
    fb_ctx.saved_cx = fb_ctx.cx;
    fb_ctx.saved_cy = fb_ctx.cy;
    break;
  case 'u':
    fb_ctx.cx = fb_ctx.saved_cx;
    fb_ctx.cy = fb_ctx.saved_cy;
    break;
  default:
    break;
  }
}

static void handle_control(u8 b)
{
  switch(b) {
  case '\n':
    fb_ctx.cx = 0;
    fb_ctx.cy++;
    if(fb_ctx.cy >= fb_ctx.rows) {
      scroll_one();
      fb_ctx.cy = fb_ctx.rows - 1;
    }
    return;
  case '\r':
    fb_ctx.cx = 0;
    return;
  case '\b':
    if(fb_ctx.cx > 0)
      fb_ctx.cx--;
    return;
  case '\t':
    fb_ctx.cx = (fb_ctx.cx + 8) & ~7;
    if(fb_ctx.cx >= fb_ctx.cols)
      fb_ctx.cx = fb_ctx.cols - 1;
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
  if(fb_ctx.utf8_rem == 0) {
    if(b < 0x20u || b == 0x7fu) {
      handle_control(b);
      return;
    }
    if(b < 0x80u) {
      /* G0 DEC ACS in effect: map printable ASCII to box-drawing/math. */
      if(fb_ctx.g0_acs && b >= 0x60u) {
        put_cp_at_cursor(acs_to_unicode(b));
        return;
      }
      put_cp_at_cursor((u32)b);
      return;
    }
    if((b & 0xe0u) == 0xc0u) {
      fb_ctx.utf8_partial = (u32)(b & 0x1fu);
      fb_ctx.utf8_rem     = 1;
      return;
    }
    if((b & 0xf0u) == 0xe0u) {
      fb_ctx.utf8_partial = (u32)(b & 0x0fu);
      fb_ctx.utf8_rem     = 2;
      return;
    }
    if((b & 0xf8u) == 0xf0u) {
      fb_ctx.utf8_partial = (u32)(b & 0x07u);
      fb_ctx.utf8_rem     = 3;
      return;
    }
    /* Stray continuation / invalid lead — show '?'. */
    put_cp_at_cursor((u32)'?');
    return;
  }
  if((b & 0xc0u) != 0x80u) {
    /* Broken sequence; recover by replaying this byte fresh. */
    fb_ctx.utf8_rem = 0;
    put_cp_at_cursor((u32)'?');
    feed_utf8(b);
    return;
  }
  fb_ctx.utf8_partial = (fb_ctx.utf8_partial << 6) | (u32)(b & 0x3fu);
  fb_ctx.utf8_rem--;
  if(fb_ctx.utf8_rem == 0) {
    u32 cp = fb_ctx.utf8_partial;
    if(cp <= 0x10ffffu)
      put_cp_at_cursor(cp);
    else
      put_cp_at_cursor((u32)'?');
  }
}

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

/* ESC state machine: drives the byte stream through ANSI / CSI / charset
 * states before falling through to UTF-8 → cell emission. */
static void feed_byte(u8 b)
{
  switch(fb_ctx.esc_state) {
  case 1: /* after ESC */
    if(b == '[') {
      fb_ctx.esc_state = 2;
      fb_ctx.esc_len   = 0;
      return;
    }
    if(b == '7') { /* DECSC */
      fb_ctx.saved_cx  = fb_ctx.cx;
      fb_ctx.saved_cy  = fb_ctx.cy;
      fb_ctx.esc_state = 0;
      return;
    }
    if(b == '8') { /* DECRC */
      fb_ctx.cx        = fb_ctx.saved_cx;
      fb_ctx.cy        = fb_ctx.saved_cy;
      fb_ctx.esc_state = 0;
      return;
    }
    if(b == '(' || b == ')') {
      fb_ctx.esc_state = 3; /* wait for designator byte */
      return;
    }
    /* Unrecognised ESC <byte> — swallow (DECKPAM/DECKPNM = / >, etc.). */
    fb_ctx.esc_state = 0;
    return;
  case 2: /* inside CSI */
    if((b >= '0' && b <= '9') || b == ';' || b == '?') {
      if(fb_ctx.esc_len < (u8)(sizeof fb_ctx.esc_buf - 1))
        fb_ctx.esc_buf[fb_ctx.esc_len++] = (char)b;
      return;
    }
    if(fb_ctx.esc_len < (u8)(sizeof fb_ctx.esc_buf - 1))
      fb_ctx.esc_buf[fb_ctx.esc_len++] = (char)b;
    handle_csi();
    fb_ctx.esc_state = 0;
    return;
  case 3: /* charset designator */
    if(b == '0')
      fb_ctx.g0_acs = 1;
    else if(b == 'B' || b == 'A' || b == 'U' || b == '1' || b == '2')
      fb_ctx.g0_acs = 0;
    fb_ctx.esc_state = 0;
    return;
  default:
    break;
  }

  if(b == 0x1bu) {
    fb_ctx.esc_state = 1;
    fb_ctx.utf8_rem  = 0;
    return;
  }
  feed_utf8(b);
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
          0xFF000000u | fb_ctx.default_bg, fb_ctx.width
      );
  scrollback_drop_pending();
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      blit_cell(c, r);
  caret_clear_drawn();
  caret_paint();
}
