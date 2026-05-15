/**
 * @file user/shell/platform/fb_tty.c
 * @brief Framebuffer terminal: Fira via FreeType + HarfBuzz, full ANSI/CSI.
 *
 * This is the user-facing terminal emulator. Bytes written by the shell or
 * relayed from child processes (ncurses, less, ...) come in through
 * @c term_feed_byte, get parsed for ANSI / CSI / charset designations, and
 * land in a cell grid that is reshaped row-by-row with HarfBuzz.
 *
 * The grid stores one ::cell_t per position (codepoint plus effective fg/bg
 * captured at write time). Per-cell colors are what make SGR-driven
 * highlights survive a row reshape; without them, every cell would render
 * with the *current* SGR state, which is back to default by the time
 * @c wrefresh fires.
 *
 * The terminal grid size is published via @c TIOCSWINSZ on init so children
 * see the real on-screen rows/cols, not a hardcoded 80x25.
 */

#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <alcor2/alcor_fb_user.h>
#include <alcor2/types.h>
#include <vega/fb_tty.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

enum
{
  kMargin  = 20,
  kPixelHt = 22,
  kTabCols = 8,
  kEscMax  = 64
};

static int             s_active;
static uint8_t        *s_fb;
static alcor_fb_info_t s_inf;

static FT_Library      s_ft;
static FT_Face         s_face;
static hb_font_t      *s_hb;
static uint8_t        *s_font_blob;
static size_t          s_font_len;
static hb_buffer_t    *s_buf;

enum
{
  kAttrBold      = 1u << 0,
  kAttrUnderline = 1u << 1,
  kAttrBlink     = 1u << 2,
};

/** @brief One grid position: codepoint, captured fg/bg, and SGR attribute bits.
 */
typedef struct
{
  uint32_t cp;
  uint32_t fg;
  uint32_t bg;
  uint8_t  attrs;
} cell_t;

/** Full-row HarfBuzz (liga+calt) — one ::cell_t per grid position. */
static hb_feature_t s_feat_liga[] = {
    {HB_TAG('l', 'i', 'g', 'a'), 1, HB_FEATURE_GLOBAL_START,
     HB_FEATURE_GLOBAL_END},
    {HB_TAG('c', 'a', 'l', 't'), 1, HB_FEATURE_GLOBAL_START,
     HB_FEATURE_GLOBAL_END},
};
static const unsigned s_feat_liga_n =
    (unsigned)(sizeof(s_feat_liga) / sizeof(s_feat_liga[0])
    ); // NOLINT(bugprone-sizeof-expression)

static int      s_line_h, s_ascent_px, s_descent_px;
static int      s_cell_w, s_cell_h;
static int      s_term_cols, s_term_rows;
static int      s_tc_x, s_tc_y;

static cell_t  *s_cells;
static int      s_saved_cx, s_saved_cy;

static uint32_t s_fg, s_bg;
static uint32_t s_term_fg, s_term_bg;
static int      s_bold;
static int      s_dim;
static int      s_underline;
static int      s_blink;
static int      s_rev;

static int      s_csr_visible;
static int      s_blink_show_bar;
static int      s_bar_on;
static int      s_bar_cx, s_bar_cy;

/* Deferred reshape: bits set by mark_dirty(), flushed in flush_dirty_rows().
 * Two u64s cover up to 128 rows (enough for any framebuffer at this cell size).
 */
static uint64_t s_dirty_rows[2];

static int      s_esc_state;
static int      s_esc_len;
static char     s_esc_buf[kEscMax];
static unsigned s_utf8_rem;
static uint32_t s_utf8_partial;
static int      s_g0_acs;  /**< 1 when G0 is DEC Special Graphics (ESC(0). */
static uint32_t s_last_cp; /**< Last codepoint emitted, replayed by CSI REP. */
static int      s_blink_phase; /**< 0: show blink cells; 1: hide them. */

static uint32_t eff_fg_raw(void)
{
  return s_rev ? s_term_bg : s_term_fg;
}

static uint32_t eff_bg_raw(void)
{
  return s_rev ? s_term_fg : s_term_bg;
}

/** @brief Lift @p c toward white; A_BOLD nudges the color a touch. */
static uint32_t brighten(uint32_t c)
{
  int r = (int)((c >> 16) & 0xffu);
  int g = (int)((c >> 8) & 0xffu);
  int b = (int)(c & 0xffu);
  r     = r + ((255 - r) * 2) / 5;
  g     = g + ((255 - g) * 2) / 5;
  b     = b + ((255 - b) * 2) / 5;
  if(r > 255)
    r = 255;
  if(g > 255)
    g = 255;
  if(b > 255)
    b = 255;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/** @brief Scale @p c toward black; used for A_DIM. */
static uint32_t darken(uint32_t c)
{
  int r = (int)((c >> 16) & 0xffu);
  int g = (int)((c >> 8) & 0xffu);
  int b = (int)(c & 0xffu);
  r     = (r * 3) / 5;
  g     = (g * 3) / 5;
  b     = (b * 3) / 5;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/** @brief Effective ink color for the next glyph (applies bold and dim). */
static uint32_t eff_ink(void)
{
  uint32_t c = eff_fg_raw();
  if(s_bold)
    c = brighten(c);
  if(s_dim)
    c = darken(c);
  return c;
}

static void put_px(int x, int y, uint32_t rgb, int a)
{
  if(x < 0 || y < 0 || (uint32_t)x >= s_inf.width ||
     (uint32_t)y >= s_inf.height)
    return;
  if(s_inf.bpp != 32 || s_inf.pitch < 4)
    return;
  size_t  o  = (size_t)y * s_inf.pitch + (size_t)x * 4;
  uint8_t nr = (uint8_t)((rgb >> 16) & 0xffu),
          ng = (uint8_t)((rgb >> 8) & 0xffu), nb = (uint8_t)(rgb & 0xffu);
  uint8_t *p  = s_fb + o;
  uint32_t aa = (uint32_t)a;
  if(aa >= 255u) {
    p[0] = nb;
    p[1] = ng;
    p[2] = nr;
    p[3] = 0xff;
    return;
  }
  uint32_t b0 = p[0], g0 = p[1], r0 = p[2];
  p[0] = (uint8_t)((nb * aa + b0 * (255u - aa)) / 255u);
  p[1] = (uint8_t)((ng * aa + g0 * (255u - aa)) / 255u);
  p[2] = (uint8_t)((nr * aa + r0 * (255u - aa)) / 255u);
  p[3] = 0xff;
}

static void fill_rect_px(int x0, int y0, int x1, int y1, uint32_t rgb)
{
  if(x0 < 0)
    x0 = 0;
  if(y0 < 0)
    y0 = 0;
  if(x1 > (int)s_inf.width)
    x1 = (int)s_inf.width;
  if(y1 > (int)s_inf.height)
    y1 = (int)s_inf.height;
  for(int y = y0; y < y1; y++)
    for(int x = x0; x < x1; x++)
      put_px(x, y, rgb, 255);
}

static void blit_gray(int x0, int y0, const FT_Bitmap *bm, uint32_t fg)
{
  for(unsigned yy = 0; yy < bm->rows; yy++) {
    for(unsigned xx = 0; xx < bm->width; xx++) {
      uint8_t a = bm->buffer[yy * bm->pitch + xx];
      if(a)
        put_px(x0 + (int)xx, y0 + (int)yy, fg, (int)a);
    }
  }
}

static int cell_px_x(int cx)
{
  return kMargin + cx * s_cell_w;
}

static int cell_px_y_top(int cy)
{
  return kMargin + cy * s_cell_h;
}

static void metrics_refresh(void)
{
  if(!s_face)
    return;
  const FT_Size_Metrics *m = &s_face->size->metrics;
  s_line_h                 = (int)(m->height >> 6) + 2;
  s_ascent_px              = (int)(m->ascender >> 6);
  s_descent_px             = (int)((-m->descender) >> 6);
  s_cell_h                 = s_line_h;

  if(FT_Load_Char(s_face, 'M', FT_LOAD_DEFAULT) == 0) {
    int w = (int)(s_face->glyph->advance.x >> 6) + 4;
    if(w < 10)
      w = 10;
    s_cell_w = w;
  } else {
    s_cell_w = 12;
  }
}

/**
 * @brief Publish the cell grid as the kernel's TTY winsize.
 *
 * Children inherit it through @c TIOCGWINSZ so ncurses lays out against the
 * real on-screen dimensions. Failure is ignored: the kernel keeps the
 * previous value, which is still better than crashing init.
 */
static void term_publish_winsize(void)
{
  struct winsize ws = {
      .ws_row    = (unsigned short)s_term_rows,
      .ws_col    = (unsigned short)s_term_cols,
      .ws_xpixel = (unsigned short)s_inf.width,
      .ws_ypixel = (unsigned short)s_inf.height,
  };
  (void)ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws);
}

/**
 * @brief Recompute cols/rows from current cell metrics and republish them.
 */
static void term_recompute_grid(void)
{
  int iw      = (int)s_inf.width - 2 * kMargin;
  int ih      = (int)s_inf.height - 2 * kMargin;
  s_term_cols = iw / s_cell_w;
  s_term_rows = ih / s_cell_h;
  if(s_term_cols < 1)
    s_term_cols = 1;
  if(s_term_rows < 1)
    s_term_rows = 1;
  term_publish_winsize();
}

/**
 * @brief Clear a single cell's pixels, using that cell's stored bg.
 *
 * Falls back to @c eff_bg_raw() before the grid is allocated so early-init
 * fills still work.
 */
static void term_clear_cell(int cx, int cy)
{
  if(cx < 0 || cy < 0 || cx >= s_term_cols || cy >= s_term_rows)
    return;
  uint32_t bg = eff_bg_raw();
  if(s_cells) {
    uint32_t stored = s_cells[(size_t)cy * (size_t)s_term_cols + (size_t)cx].bg;
    if(stored)
      bg = stored;
  }
  int x0 = cell_px_x(cx);
  int y0 = cell_px_y_top(cy);
  fill_rect_px(x0, y0, x0 + s_cell_w, y0 + s_cell_h, bg);
}

/**
 * @brief Draw a shaped row, anchoring each cluster to its cell and coloring
 *        each glyph with the source cell's fg.
 *
 * Font x_advance is only applied within the same cluster (stacked marks).
 * Accumulating raw advances across clusters would misplace glyphs relative
 * to the monospace grid.
 */
static void
    draw_shaped_at_pen(int y_baseline, hb_buffer_t *buf, const cell_t *row)
{
  unsigned             len;
  hb_glyph_info_t     *info = hb_buffer_get_glyph_infos(buf, &len);
  hb_glyph_position_t *pos  = hb_buffer_get_glyph_positions(buf, &len);

  uint32_t             prev_cluster = ~(uint32_t)0u;
  int                  px           = 0;
  int                  py           = y_baseline;
  uint32_t             cur_fg       = eff_ink();
  int                  cur_bold     = 0;

  for(unsigned i = 0; i < len; i++) {
    if(info[i].cluster != prev_cluster) {
      prev_cluster = info[i].cluster;
      int cp_idx   = (int)info[i].cluster;
      if(cp_idx < 0)
        cp_idx = 0;
      if(cp_idx >= s_term_cols)
        cp_idx = s_term_cols - 1;
      px       = cell_px_x(cp_idx);
      cur_fg   = row[cp_idx].fg ? row[cp_idx].fg : eff_ink();
      cur_bold = (row[cp_idx].attrs & kAttrBold) != 0;
    }

    px += pos[i].x_offset >> 6;
    py -= pos[i].y_offset >> 6;

    hb_codepoint_t gid = info[i].codepoint;
    if(FT_Load_Glyph(s_face, gid, FT_LOAD_DEFAULT) != 0 ||
       FT_Render_Glyph(s_face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
      px += pos[i].x_advance >> 6;
      py -= pos[i].y_advance >> 6;
      continue;
    }

    FT_GlyphSlot slot = s_face->glyph;
    int          bx   = px + slot->bitmap_left;
    int          by   = py - slot->bitmap_top;
    blit_gray(bx, by, &slot->bitmap, cur_fg);
    if(cur_bold)
      blit_gray(bx + 1, by, &slot->bitmap, cur_fg);

    px += pos[i].x_advance >> 6;
    py -= pos[i].y_advance >> 6;
  }
}

/** @brief True when @p cp lies in the Unicode Box Drawing block. */
static int is_box_drawing(uint32_t cp)
{
  return cp >= 0x2500u && cp <= 0x257Fu;
}

/** @brief Stroke thickness in pixels for box-drawing rendering. */
static int box_stroke_px(int bold)
{
  int t = s_cell_w / 8;
  if(t < 1)
    t = 1;
  return bold ? t + 1 : t;
}

/**
 * @brief Render a Unicode box-drawing glyph as geometric primitives.
 *
 * Most monospace fonts (Fira included) ship box-drawing glyphs with side
 * bearings, which leaves a hairline gap between adjacent cells. Drawing the
 * strokes directly with @c fill_rect_px guarantees pixel-tight joins at
 * corners and along long runs.
 *
 * Only the subset of the U+2500 block that ncurses' @c box / @c wborder
 * emits is covered; anything else falls through to the regular font path
 * the caller already invoked.
 */
static void
    draw_box_primitive(int cx, int cy, uint32_t cp, uint32_t fg, int bold)
{
  int x0  = cell_px_x(cx);
  int y0  = cell_px_y_top(cy);
  int x1  = x0 + s_cell_w;
  int y1  = y0 + s_cell_h;
  int mx  = x0 + s_cell_w / 2;
  int my  = y0 + s_cell_h / 2;
  int th  = box_stroke_px(bold);
  int hth = th / 2;
  int hx0 = mx - hth;
  int hx1 = hx0 + th;
  int hy0 = my - hth;
  int hy1 = hy0 + th;

  switch(cp) {
  case 0x2500u: /* ─ */
    fill_rect_px(x0, hy0, x1, hy1, fg);
    break;
  case 0x2502u: /* │ */
    fill_rect_px(hx0, y0, hx1, y1, fg);
    break;
  case 0x250Cu: /* ┌ */
    fill_rect_px(hx0, hy0, x1, hy1, fg);
    fill_rect_px(hx0, hy0, hx1, y1, fg);
    break;
  case 0x2510u: /* ┐ */
    fill_rect_px(x0, hy0, hx1, hy1, fg);
    fill_rect_px(hx0, hy0, hx1, y1, fg);
    break;
  case 0x2514u: /* └ */
    fill_rect_px(hx0, hy0, x1, hy1, fg);
    fill_rect_px(hx0, y0, hx1, hy1, fg);
    break;
  case 0x2518u: /* ┘ */
    fill_rect_px(x0, hy0, hx1, hy1, fg);
    fill_rect_px(hx0, y0, hx1, hy1, fg);
    break;
  case 0x251Cu: /* ├ */
    fill_rect_px(hx0, y0, hx1, y1, fg);
    fill_rect_px(hx0, hy0, x1, hy1, fg);
    break;
  case 0x2524u: /* ┤ */
    fill_rect_px(hx0, y0, hx1, y1, fg);
    fill_rect_px(x0, hy0, hx1, hy1, fg);
    break;
  case 0x252Cu: /* ┬ */
    fill_rect_px(x0, hy0, x1, hy1, fg);
    fill_rect_px(hx0, hy0, hx1, y1, fg);
    break;
  case 0x2534u: /* ┴ */
    fill_rect_px(x0, hy0, x1, hy1, fg);
    fill_rect_px(hx0, y0, hx1, hy1, fg);
    break;
  case 0x253Cu: /* ┼ */
    fill_rect_px(x0, hy0, x1, hy1, fg);
    fill_rect_px(hx0, y0, hx1, y1, fg);
    break;
  default:
    break;
  }
}

static void redraw_line_shaped(int cy); /* forward; defined after cell_set. */

static void mark_dirty(int cy)
{
  if((unsigned)cy < 64u)
    s_dirty_rows[0] |= (1ULL << cy);
  else if((unsigned)cy < 128u)
    s_dirty_rows[1] |= (1ULL << (cy - 64));
}

static void unmark_dirty(int cy)
{
  if((unsigned)cy < 64u)
    s_dirty_rows[0] &= ~(1ULL << cy);
  else if((unsigned)cy < 128u)
    s_dirty_rows[1] &= ~(1ULL << (cy - 64));
}

static void flush_dirty_rows(void)
{
  for(int w = 0; w < 2; w++) {
    while(s_dirty_rows[w]) {
      int bit = __builtin_ctzll(s_dirty_rows[w]);
      s_dirty_rows[w] &= s_dirty_rows[w] - 1;
      redraw_line_shaped(w * 64 + bit);
    }
  }
}

/**
 * @brief Store @p cp at (cx,cy) along with the current effective fg/bg.
 *
 * Snapshotting colors here is what lets a later @c redraw_line_shaped reach
 * back through time to the SGR state that was active when the cell was
 * written, instead of the now-default state.
 */
/** @brief Capture the current SGR attribute bits for a cell. */
static uint8_t cur_attrs(void)
{
  uint8_t a = 0;
  if(s_bold)
    a |= kAttrBold;
  if(s_underline)
    a |= kAttrUnderline;
  if(s_blink)
    a |= kAttrBlink;
  return a;
}

static void cell_set(int cx, int cy, uint32_t cp)
{
  if(!s_cells || cx < 0 || cy < 0 || cx >= s_term_cols || cy >= s_term_rows)
    return;
  cell_t *c = &s_cells[(size_t)cy * (size_t)s_term_cols + (size_t)cx];
  c->cp     = cp ? cp : (uint32_t)' ';
  c->fg     = eff_ink();
  c->bg     = eff_bg_raw();
  c->attrs  = cur_attrs();
}

/**
 * @brief Repaint row @p cy: background per cell, then one shaped pass for fg.
 *
 * Codepoints are copied into a scratch buffer (stack for typical widths,
 * heap as a fallback) so HarfBuzz can shape the whole row in one call while
 * each glyph still picks up its source cell's fg.
 */
static void redraw_line_shaped(int cy)
{
  if(!s_buf || !s_cells || cy < 0 || cy >= s_term_rows)
    return;
  unmark_dirty(cy);

  int     cols = s_term_cols;
  cell_t *row  = s_cells + (size_t)cy * (size_t)cols;

  for(int x = 0; x < cols; x++)
    term_clear_cell(x, cy);

  uint32_t  stack_cps[256];
  uint32_t *cps      = stack_cps;
  uint32_t *cps_heap = NULL;
  if(cols > (int)(sizeof stack_cps / sizeof stack_cps[0])) {
    cps_heap = (uint32_t *)malloc((size_t)cols * sizeof(uint32_t));
    if(!cps_heap)
      return;
    cps = cps_heap;
  }
  /* Build the shape buffer:
   *   - box drawing cells render as primitives below, so mask them to space;
   *   - blink cells in the "hide" phase also mask to space so the glyph
   *     disappears while the background stays put. */
  for(int x = 0; x < cols; x++) {
    uint32_t cp = row[x].cp ? row[x].cp : (uint32_t)' ';
    if(is_box_drawing(cp))
      cp = (uint32_t)' ';
    if(s_blink_phase && (row[x].attrs & kAttrBlink))
      cp = (uint32_t)' ';
    cps[x] = cp;
  }

  hb_buffer_clear_contents(s_buf);
  hb_buffer_add_utf32(s_buf, cps, cols, 0, cols);
  hb_buffer_guess_segment_properties(s_buf);
  hb_shape(s_hb, s_buf, s_feat_liga, s_feat_liga_n);

  draw_shaped_at_pen(cell_px_y_top(cy) + s_ascent_px, s_buf, row);
  free(cps_heap);

  for(int x = 0; x < cols; x++) {
    if(!is_box_drawing(row[x].cp))
      continue;
    if(s_blink_phase && (row[x].attrs & kAttrBlink))
      continue;
    uint32_t fg   = row[x].fg ? row[x].fg : eff_ink();
    int      bold = (row[x].attrs & kAttrBold) != 0;
    draw_box_primitive(x, cy, row[x].cp, fg, bold);
  }

  /* Underline pass: draw 1px strokes under runs of underlined cells. The
   * line sits just under the baseline so it doesn't clip descenders too
   * hard while staying clear of the next row. */
  int uy = cell_px_y_top(cy) + s_cell_h - 2;
  int x  = 0;
  while(x < cols) {
    if(!(row[x].attrs & kAttrUnderline)) {
      x++;
      continue;
    }
    int x1 = x;
    while(x1 < cols && (row[x1].attrs & kAttrUnderline))
      x1++;
    int px0 = cell_px_x(x);
    int px1 = cell_px_x(x1);
    fill_rect_px(px0, uy, px1, uy + 1, row[x].fg ? row[x].fg : eff_ink());
    x = x1;
  }
}

/** After EL/ED: cells cleared then one shaped repaint of the row. */
static void redraw_line_cells(int cy)
{
  if(cy < 0 || cy >= s_term_rows)
    return;
  redraw_line_shaped(cy);
}

static void cursor_hide_bar(void)
{
  if(!s_active || !s_bar_on)
    return;
  redraw_line_shaped(s_bar_cy);
  s_bar_on = 0;
}

static void cursor_draw_bar(void)
{
  if(!s_csr_visible || !s_blink_show_bar || s_tc_x < 0 || s_tc_y < 0 ||
     s_tc_x >= s_term_cols || s_tc_y >= s_term_rows)
    return;
  /* Block cursor (~Linux TTY): left of cell, high contrast vs Nord background.
   */
  int x0 = cell_px_x(s_tc_x);
  int y0 = cell_px_y_top(s_tc_y) + 1;
  int y1 = cell_px_y_top(s_tc_y) + s_cell_h - 1;
  int w  = s_cell_w / 8;
  if(w < 3)
    w = 3;
  if(w > s_cell_w / 3)
    w = s_cell_w / 3;
  uint32_t col = 0x00eceff4u;
  for(int y = y0; y < y1; y++) {
    for(int x = x0; x < x0 + w && x < (int)s_inf.width; x++)
      put_px(x, y, col, 255);
  }
  s_bar_on = 1;
  s_bar_cx = s_tc_x;
  s_bar_cy = s_tc_y;
}

static void term_scroll_one(void)
{
  if(s_term_rows < 2 || !s_fb || s_inf.bpp != 32)
    return;
  flush_dirty_rows(); /* must run before pixel memmove shifts row indices */
  cursor_hide_bar();
  u32 y0    = (u32)kMargin;
  u32 y_end = y0 + (u32)s_term_rows * (u32)s_cell_h;
  u32 dy    = (u32)s_cell_h;
  u32 pitch = s_inf.pitch;
  for(u32 y = y0; y + dy < y_end; y++)
    memmove(s_fb + (size_t)y * pitch, s_fb + (size_t)(y + dy) * pitch, pitch);
  fill_rect_px(
      kMargin, (int)(y_end - dy), (int)s_inf.width - kMargin, (int)y_end,
      eff_bg_raw()
  );

  if(s_cells) {
    size_t rowb = (size_t)s_term_cols * sizeof(cell_t);
    memmove(s_cells, s_cells + s_term_cols, rowb * (size_t)(s_term_rows - 1));
    cell_t *last = s_cells + (size_t)(s_term_rows - 1) * (size_t)s_term_cols;
    for(int x = 0; x < s_term_cols; x++) {
      last[x].cp    = (uint32_t)' ';
      last[x].fg    = eff_ink();
      last[x].bg    = eff_bg_raw();
      last[x].attrs = 0;
    }
  }
}

static void term_cursor_down_maybe_scroll(void)
{
  s_tc_y++;
  if(s_tc_y >= s_term_rows) {
    term_scroll_one();
    s_tc_y = s_term_rows - 1;
  }
}

static uint32_t ansi256_to_rgb(unsigned idx)
{
  static const uint32_t ansi16[16] = {
      0x000000u, 0xcc5555u, 0x55cc55u, 0xcccc55u, 0x5555ffu, 0xcc55ccu,
      0x55ccccu, 0xddddddu, 0x555555u, 0xff8888u, 0x88ff88u, 0xffff88u,
      0x8888ffu, 0xff88ffu, 0x88ffffu, 0xffffffu
  };
  if(idx < 16u)
    return ansi16[idx];
  if(idx < 232u) {
    unsigned i   = idx - 16u;
    unsigned r6  = i / 36u;
    unsigned rem = i % 36u;
    unsigned g6  = rem / 6u;
    unsigned b6  = rem % 6u;
    uint32_t r   = (r6 == 0u) ? 0u : (55u + 40u * (r6 - 1u));
    uint32_t g   = (g6 == 0u) ? 0u : (55u + 40u * (g6 - 1u));
    uint32_t b   = (b6 == 0u) ? 0u : (55u + 40u * (b6 - 1u));
    return (r << 16) | (g << 8) | b;
  }
  uint32_t v = 8u + 10u * (idx - 232u);
  return (v << 16) | (v << 8) | v;
}

static uint32_t ansi8_fg(unsigned i)
{
  static const uint32_t c[8] = {0x003b4252u, 0x00bf616au, 0x00a3be8cu,
                                0x00ebcb8bu, 0x005e81acu, 0x00b48eadu,
                                0x0088c0d0u, 0x00eceff4u};
  if(i < 8u)
    return c[i];
  return s_term_fg;
}

static uint32_t ansi8_fg_bright(unsigned i)
{
  static const uint32_t c[8] = {0x004c566au, 0x00d08770u, 0x00b8d99bu,
                                0x00f2e9c4u, 0x0081a1c1u, 0x00c89fc8u,
                                0x008fbcbbu, 0x00e5e9f0u};
  if(i < 8u)
    return c[i];
  return s_term_fg;
}

static uint32_t ansi8_bg(unsigned i)
{
  static const uint32_t c[8] = {0x002e3440u, 0x006a4a4eu, 0x003b5348u,
                                0x0057564eu, 0x003d4f66u, 0x00403d52u,
                                0x003b5254u, 0x00e5e9f0u};
  if(i < 8u)
    return c[i];
  return s_term_bg;
}

static uint32_t ansi8_bg_bright(unsigned i)
{
  return ansi8_bg(i);
}

static int csi_parse_semicolon_params(int *pv, int maxn)
{
  int pn = s_esc_len - 1;
  int np = 0;
  int i  = 0;
  while(i < pn && np < maxn) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < pn && s_esc_buf[i] >= '0' && s_esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(s_esc_buf[i] - '0');
      i++;
      dig++;
    }
    pv[np++] = (int)((dig != 0) ? acc % 256u : 0u);
    if(i < pn && s_esc_buf[i] == ';')
      i++;
  }
  return np;
}

static int csi_leading_param_default_1(void)
{
  int pv[8];
  int np = csi_parse_semicolon_params(pv, 8);
  if(np == 0 || pv[0] == 0)
    return 1;
  int v = pv[0];
  return (v < 1) ? 1 : v;
}

static void cup_apply_csi(void)
{
  int row = 1, col = 1;
  int plen = s_esc_len - 1;

  cursor_hide_bar();
  if(plen > 0) {
    int i     = 0;
    int r     = 0;
    int has_r = 0;
    while(i < plen && s_esc_buf[i] >= '0' && s_esc_buf[i] <= '9') {
      r = r * 10 + (int)(s_esc_buf[i] - '0');
      i++;
      has_r = 1;
    }
    if(has_r)
      row = r;
    if(i < plen && s_esc_buf[i] == ';') {
      i++;
      int c     = 0;
      int has_c = 0;
      while(i < plen && s_esc_buf[i] >= '0' && s_esc_buf[i] <= '9') {
        c = c * 10 + (int)(s_esc_buf[i] - '0');
        i++;
        has_c = 1;
      }
      if(has_c)
        col = c;
    } else if(has_r)
      col = 1;
  }

  if(row < 1)
    row = 1;
  if(col < 1)
    col = 1;
  if(row > s_term_rows)
    row = s_term_rows;
  if(col > s_term_cols)
    col = s_term_cols;

  s_tc_y = row - 1;
  s_tc_x = col - 1;
}

static void erase_cells_rect(int y0, int x0, int y1, int x1)
{
  if(!s_cells)
    return;
  for(int y = y0; y <= y1; y++) {
    if(y < 0 || y >= s_term_rows)
      continue;
    for(int x = x0; x <= x1; x++) {
      if(x < 0 || x >= s_term_cols)
        continue;
      cell_set(x, y, (uint32_t)' ');
      term_clear_cell(x, y);
    }
    redraw_line_cells(y);
  }
}

static void erase_screen_from_cursor(int mode)
{
  if(mode == 0) {
    erase_cells_rect(s_tc_y, s_tc_x, s_tc_y, s_term_cols - 1);
    if(s_tc_y < s_term_rows - 1)
      erase_cells_rect(s_tc_y + 1, 0, s_term_rows - 1, s_term_cols - 1);
  } else if(mode == 1) {
    if(s_tc_y > 0)
      erase_cells_rect(0, 0, s_tc_y - 1, s_term_cols - 1);
    erase_cells_rect(s_tc_y, 0, s_tc_y, s_tc_x);
  }
}

static void handle_sgr(void)
{
  if(s_esc_len < 1 || s_esc_buf[s_esc_len - 1] != 'm')
    return;

  int pn = s_esc_len - 1;
  if(pn <= 0) {
    s_term_fg   = s_fg;
    s_term_bg   = s_bg;
    s_bold      = 0;
    s_dim       = 0;
    s_underline = 0;
    s_blink     = 0;
    s_rev       = 0;
    return;
  }

  int pv[32];
  int np = csi_parse_semicolon_params(pv, 32);

  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    switch(p) {
    case 0: /* reset */
      s_term_fg   = s_fg;
      s_term_bg   = s_bg;
      s_bold      = 0;
      s_dim       = 0;
      s_underline = 0;
      s_blink     = 0;
      s_rev       = 0;
      break;
    case 1: /* bold */
      s_bold = 1;
      break;
    case 2: /* dim / faint */
      s_dim = 1;
      break;
    case 4: /* underline */
      s_underline = 1;
      break;
    case 5: /* blink (slow) */
    case 6: /* blink (rapid) */
      s_blink = 1;
      break;
    case 7: /* reverse */
      s_rev = 1;
      break;
    case 22: /* bold + dim off */
      s_bold = 0;
      s_dim  = 0;
      break;
    case 24: /* underline off */
      s_underline = 0;
      break;
    case 25: /* blink off */
      s_blink = 0;
      break;
    case 27: /* reverse off */
      s_rev = 0;
      break;
    case 39:
      s_term_fg = s_fg;
      break;
    case 49:
      s_term_bg = s_bg;
      break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
      s_term_fg = ansi8_fg((unsigned)(p - 30));
      break;
    case 90:
    case 91:
    case 92:
    case 93:
    case 94:
    case 95:
    case 96:
    case 97:
      s_term_fg = ansi8_fg_bright((unsigned)(p - 90));
      break;
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
      s_term_bg = ansi8_bg((unsigned)(p - 40));
      break;
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
      s_term_bg = ansi8_bg_bright((unsigned)(p - 100));
      break;
    case 38:
      if(pi + 2 < np && pv[pi + 1] == 5) {
        s_term_fg = ansi256_to_rgb((unsigned)pv[pi + 2]);
        pi += 2;
      } else if(pi + 4 < np && pv[pi + 1] == 2) {
        int r = pv[pi + 2], g = pv[pi + 3], b = pv[pi + 4];
        s_term_fg = ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) |
                    (uint32_t)(b & 255);
        pi += 4;
      }
      break;
    case 48:
      if(pi + 2 < np && pv[pi + 1] == 5) {
        s_term_bg = ansi256_to_rgb((unsigned)pv[pi + 2]);
        pi += 2;
      } else if(pi + 4 < np && pv[pi + 1] == 2) {
        int r = pv[pi + 2], g = pv[pi + 3], b = pv[pi + 4];
        s_term_bg = ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) |
                    (uint32_t)(b & 255);
        pi += 4;
      }
      break;
    default:
      break;
    }
  }
}

static void handle_dec_private(char cmd)
{
  int pv[8];
  int np = 0;
  if(s_esc_len < 3 || s_esc_buf[0] != '?')
    return;
  int i = 1;
  while(i < s_esc_len - 1 && np < 8) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < s_esc_len - 1 && s_esc_buf[i] >= '0' && s_esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(s_esc_buf[i] - '0');
      i++;
      dig++;
    }
    if(dig)
      pv[np++] = (int)(acc % 10000u);
    if(i < s_esc_len - 1 && s_esc_buf[i] == ';')
      i++;
    else if(!dig && i < s_esc_len - 1)
      i++;
  }

  int on = (cmd == 'h');
  for(int k = 0; k < np; k++) {
    if(pv[k] == 25) {
      s_csr_visible = on;
      if(!on)
        cursor_hide_bar();
    } else if(pv[k] == 1049) {
      /* xterm smcup/rmcup use ?1049 save/restore. We implemented that as a full
       * grid swap + clear on enter. That (1) flashes blank until ncurses draws,
       * and (2) on leave restores the pre-app shell buffer so all child output
       * vanishes as soon as endwin() runs — unusable for relayed ncurses.
       * Ignore 1049: draw into the live grid like a scrollback terminal. */
      (void)on;
    }
  }
}

static void term_emit_cp(uint32_t cp);

static void handle_ansi_sequence(void)
{
  if(s_esc_len < 1)
    return;

  char cmd = s_esc_buf[s_esc_len - 1];

  if((cmd == 'h' || cmd == 'l') && s_esc_len >= 2 && s_esc_buf[0] == '?') {
    cursor_hide_bar();
    handle_dec_private(cmd);
    return;
  }

  cursor_hide_bar();
  switch(cmd) {
  case 'A': {
    int n = csi_leading_param_default_1();
    if(s_tc_y >= n)
      s_tc_y -= n;
    else
      s_tc_y = 0;
    break;
  }
  case 'B': {
    int n        = csi_leading_param_default_1();
    int max_step = s_term_rows - 1 - s_tc_y;
    if(n > max_step)
      n = max_step;
    if(n > 0)
      s_tc_y += n;
    break;
  }
  case 'C': {
    int n        = csi_leading_param_default_1();
    int max_step = s_term_cols - 1 - s_tc_x;
    if(n > max_step)
      n = max_step;
    if(n > 0)
      s_tc_x += n;
    break;
  }
  case 'D': {
    int n = csi_leading_param_default_1();
    if(s_tc_x >= n)
      s_tc_x -= n;
    else
      s_tc_x = 0;
    break;
  }
  case 'H':
  case 'f':
    cup_apply_csi();
    break;
  case 'J': {
    int pv[4];
    int np   = csi_parse_semicolon_params(pv, 4);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 2) {
      sh_fb_tty_clear();
    } else {
      erase_screen_from_cursor(mode);
    }
    break;
  }
  case 'K': {
    int pv[4];
    int np   = csi_parse_semicolon_params(pv, 4);
    int mode = (np > 0) ? pv[0] : 0;
    if(mode == 0) {
      erase_cells_rect(s_tc_y, s_tc_x, s_tc_y, s_term_cols - 1);
    } else if(mode == 1) {
      erase_cells_rect(s_tc_y, 0, s_tc_y, s_tc_x);
    } else if(mode == 2) {
      erase_cells_rect(s_tc_y, 0, s_tc_y, s_term_cols - 1);
    }
    break;
  }
  case 'm':
    handle_sgr();
    break;
  case 'b': {
    /* REP: repeat the last printed codepoint Pn times. ncurses uses the
     * xterm-256color `rep` capability for long box-border runs, so without
     * this every horizontal/vertical border collapses to one cell. */
    if(s_last_cp == 0)
      break;
    int      n  = csi_leading_param_default_1();
    uint32_t cp = s_last_cp;
    for(int i = 0; i < n; i++)
      term_emit_cp(cp);
    break;
  }
  case 'G': {
    /* CHA: absolute column position (1-based). */
    int n = csi_leading_param_default_1();
    if(n > s_term_cols)
      n = s_term_cols;
    s_tc_x = n - 1;
    break;
  }
  case 'd': {
    /* VPA: absolute line position (1-based). */
    int n = csi_leading_param_default_1();
    if(n > s_term_rows)
      n = s_term_rows;
    s_tc_y = n - 1;
    break;
  }
  case 'X': {
    /* ECH: erase Pn chars at cursor without moving it. */
    int n  = csi_leading_param_default_1();
    int x1 = s_tc_x + n - 1;
    if(x1 >= s_term_cols)
      x1 = s_term_cols - 1;
    erase_cells_rect(s_tc_y, s_tc_x, s_tc_y, x1);
    break;
  }
  case 's': /* SCO save cursor. */
    s_saved_cx = s_tc_x;
    s_saved_cy = s_tc_y;
    break;
  case 'u': /* SCO restore cursor. */
    s_tc_x = s_saved_cx;
    s_tc_y = s_saved_cy;
    break;
  default:
    break;
  }
}

static void term_emit_cp(uint32_t cp)
{
  cursor_hide_bar();
  if(cp == 0)
    return; /* terminfo tputs NUL padding — not a glyph */
  if(cp == '\r') {
    s_tc_x = 0;
    return;
  }
  if(cp == '\n') {
    s_tc_x = 0;
    term_cursor_down_maybe_scroll();
    return;
  }
  if(cp == '\b' || cp == 127) {
    if(s_tc_x > 0) {
      s_tc_x--;
      cell_set(s_tc_x, s_tc_y, (uint32_t)' ');
      redraw_line_shaped(s_tc_y);
    } else if(s_tc_y > 0) {
      s_tc_y--;
      s_tc_x = s_term_cols - 1;
      cell_set(s_tc_x, s_tc_y, (uint32_t)' ');
      redraw_line_shaped(s_tc_y);
    }
    return;
  }
  if(cp == '\t') {
    int t = (s_tc_x + kTabCols) / kTabCols * kTabCols;
    if(t >= s_term_cols) {
      s_tc_x = 0;
      term_cursor_down_maybe_scroll();
    } else {
      while(s_tc_x < t && s_tc_x < s_term_cols) {
        cell_set(s_tc_x, s_tc_y, (uint32_t)' ');
        s_tc_x++;
      }
      redraw_line_shaped(s_tc_y);
    }
    return;
  }

  if(s_tc_x >= s_term_cols) {
    s_tc_x = 0;
    term_cursor_down_maybe_scroll();
  }
  cell_set(s_tc_x, s_tc_y, cp);
  mark_dirty(s_tc_y);
  s_last_cp = cp;
  s_tc_x++;
  if(s_tc_x >= s_term_cols) {
    s_tc_x = 0;
    term_cursor_down_maybe_scroll();
  }
}

/**
 * @brief Map a DEC Special Graphics byte (active after @c ESC(0) to Unicode.
 *
 * Fira ships the box-drawing and math blocks, so the returned codepoints
 * shape into proper glyphs through the normal HarfBuzz path.
 */
static uint32_t acs_to_unicode(uint8_t b)
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
  case 'h':
    return 0x2592u; /* ▒ (NL placeholder) */
  case 'i':
    return 0x240Bu; /* VT symbol */
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
  case 'o':
    return 0x23BAu; /* scan 1 */
  case 'p':
    return 0x23BBu; /* scan 3 */
  case 'q':
    return 0x2500u; /* ─ */
  case 'r':
    return 0x23BCu; /* scan 7 */
  case 's':
    return 0x23BDu; /* scan 9 */
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
  case '{':
    return 0x03C0u; /* π */
  case '|':
    return 0x2260u; /* ≠ */
  case '}':
    return 0x00A3u; /* £ */
  case '~':
    return 0x00B7u; /* · */
  default:
    return (uint32_t)b;
  }
}

static void feed_utf8_byte(uint8_t b)
{
  if(s_utf8_rem == 0) {
    if(b < 0x80u) {
      if(s_g0_acs && b >= 0x60u && b <= 0x7Eu) {
        term_emit_cp(acs_to_unicode(b));
        return;
      }
      term_emit_cp((uint32_t)b);
      return;
    }
    if((b & 0xe0u) == 0xc0u) {
      s_utf8_partial = (uint32_t)(b & 0x1fu);
      s_utf8_rem     = 1;
      return;
    }
    if((b & 0xf0u) == 0xe0u) {
      s_utf8_partial = (uint32_t)(b & 0x0fu);
      s_utf8_rem     = 2;
      return;
    }
    if((b & 0xf8u) == 0xf0u) {
      s_utf8_partial = (uint32_t)(b & 0x07u);
      s_utf8_rem     = 3;
      return;
    }
    term_emit_cp((uint32_t)'?');
    return;
  }

  if((b & 0xc0u) != 0x80u) {
    s_utf8_rem = 0;
    term_emit_cp((uint32_t)'?');
    feed_utf8_byte(b);
    return;
  }

  s_utf8_partial = (s_utf8_partial << 6u) | (uint32_t)(b & 0x3fu);
  s_utf8_rem--;
  if(s_utf8_rem != 0)
    return;

  uint32_t cp = s_utf8_partial;
  s_utf8_rem  = 0;
  if(cp <= 0x10ffffu)
    term_emit_cp(cp);
  else
    term_emit_cp((uint32_t)'?');
}

static void term_feed_byte(unsigned char b)
{
  if(s_esc_state == 1) {
    if(b == '[') {
      s_esc_state = 2;
      s_esc_len   = 0;
      return;
    }
    if(b == '7') {
      s_saved_cx  = s_tc_x;
      s_saved_cy  = s_tc_y;
      s_esc_state = 0;
      return;
    }
    if(b == '8') {
      cursor_hide_bar();
      s_tc_x      = s_saved_cx;
      s_tc_y      = s_saved_cy;
      s_esc_state = 0;
      return;
    }
    if(b == '(' || b == ')') {
      /* G0/G1 charset designation — wait for the designator byte. */
      s_esc_state = 3;
      return;
    }
    /* Unrecognized single-byte ESC follower (e.g. ESC > / ESC = / ESC D /
     * ESC M / ESC c). ncurses sends DECKPNM/DECKPAM around every keypad()
     * program, so falling through to feed_utf8_byte here paints a stray
     * '>' or '=' at the cursor. Swallow it instead. */
    s_esc_state = 0;
    return;
  } else if(s_esc_state == 3) {
    /* '0' selects DEC Special Graphics; ASCII/UK/Latin-1 turn it back off. */
    if(b == '0')
      s_g0_acs = 1;
    else if(b == 'B' || b == 'A' || b == 'U' || b == '1' || b == '2')
      s_g0_acs = 0;
    s_esc_state = 0;
    return;
  } else if(s_esc_state == 2) {
    if((b >= '0' && b <= '9') || b == ';' || b == '?') {
      if(s_esc_len < kEscMax - 1)
        s_esc_buf[s_esc_len++] = (char)b;
      return;
    }
    if(s_esc_len < kEscMax - 1)
      s_esc_buf[s_esc_len++] = (char)b;
    handle_ansi_sequence();
    s_esc_state = 0;
    return;
  }

  if(b == 0x1bu) {
    s_esc_state = 1;
    s_utf8_rem  = 0;
    return;
  }

  feed_utf8_byte(b);
}

static int load_font_file(const char *path, uint8_t **out, size_t *osz)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  off_t sz = lseek(fd, 0, SEEK_END);
  if(sz <= 0 || sz > (off_t)(16 * 1024 * 1024)) {
    close(fd);
    return -1;
  }
  lseek(fd, 0, SEEK_SET);
  uint8_t *buf = malloc((size_t)sz);
  if(!buf) {
    close(fd);
    return -1;
  }
  ssize_t n = read(fd, buf, (size_t)sz);
  close(fd);
  if(n != sz) {
    free(buf);
    return -1;
  }
  *out = buf;
  *osz = (size_t)sz;
  return 0;
}

bool sh_fb_tty_init(const char *font_path)
{
  s_fg             = 0x00d8dee9u;
  s_bg             = 0x002e3440u;
  s_term_fg        = s_fg;
  s_term_bg        = s_bg;
  s_bold           = 0;
  s_dim            = 0;
  s_underline      = 0;
  s_blink          = 0;
  s_rev            = 0;
  s_utf8_rem       = 0;
  s_esc_state      = 0;
  s_g0_acs         = 0;
  s_last_cp        = 0;
  s_blink_phase    = 0;
  s_cells          = NULL;
  s_csr_visible    = 1;
  s_blink_show_bar = 0;
  s_bar_on         = 0;
  s_saved_cx       = 0;
  s_saved_cy       = 0;

  if(!font_path || alcor_fb_info(&s_inf) != 0)
    return false;
  if(s_inf.bpp != 32)
    return false;

  void *mapped = alcor_fb_mmap();
  if(!mapped || mapped == (void *)-1)
    return false;
  s_fb = (uint8_t *)mapped;

  if(load_font_file(font_path, &s_font_blob, &s_font_len) != 0) {
    s_fb = NULL;
    return false;
  }

  if(FT_Init_FreeType(&s_ft) != 0 ||
     FT_New_Memory_Face(s_ft, s_font_blob, (FT_Long)s_font_len, 0, &s_face) !=
         0) {
    free(s_font_blob);
    s_font_blob = NULL;
    s_fb        = NULL;
    return false;
  }

  if(FT_Set_Pixel_Sizes(s_face, 0, kPixelHt) != 0) {
    FT_Done_Face(s_face);
    FT_Done_FreeType(s_ft);
    free(s_font_blob);
    s_face      = NULL;
    s_ft        = NULL;
    s_font_blob = NULL;
    s_fb        = NULL;
    return false;
  }

  s_hb = hb_ft_font_create_referenced(s_face);
  if(!s_hb) {
    FT_Done_Face(s_face);
    FT_Done_FreeType(s_ft);
    free(s_font_blob);
    s_face      = NULL;
    s_ft        = NULL;
    s_font_blob = NULL;
    s_fb        = NULL;
    return false;
  }

  s_buf = hb_buffer_create();
  if(!s_buf) {
    hb_font_destroy(s_hb);
    s_hb = NULL;
    FT_Done_Face(s_face);
    FT_Done_FreeType(s_ft);
    free(s_font_blob);
    s_face      = NULL;
    s_ft        = NULL;
    s_font_blob = NULL;
    s_fb        = NULL;
    return false;
  }

  hb_ft_font_changed(s_hb);
  metrics_refresh();
  term_recompute_grid();

  size_t ncell = (size_t)s_term_rows * (size_t)s_term_cols;
  s_cells      = calloc(ncell, sizeof(cell_t));
  if(!s_cells) {
    hb_buffer_destroy(s_buf);
    s_buf = NULL;
    hb_font_destroy(s_hb);
    s_hb = NULL;
    FT_Done_Face(s_face);
    FT_Done_FreeType(s_ft);
    free(s_font_blob);
    s_face      = NULL;
    s_ft        = NULL;
    s_font_blob = NULL;
    s_fb        = NULL;
    return false;
  }
  for(size_t i = 0; i < ncell; i++) {
    s_cells[i].cp    = (uint32_t)' ';
    s_cells[i].fg    = eff_ink();
    s_cells[i].bg    = eff_bg_raw();
    s_cells[i].attrs = 0;
  }

  s_tc_x = 0;
  s_tc_y = 0;

  fill_rect_px(0, 0, (int)s_inf.width, (int)s_inf.height, s_bg);

  s_active = 1;
  return true;
}

bool sh_fb_tty_active(void)
{
  return s_active != 0;
}

void sh_fb_tty_on_fork_child(void)
{
  s_active = 0;
}

void sh_fb_tty_shutdown(void)
{
  if(s_buf) {
    hb_buffer_destroy(s_buf);
    s_buf = NULL;
  }
  if(s_hb) {
    hb_font_destroy(s_hb);
    s_hb = NULL;
  }
  if(s_face) {
    FT_Done_Face(s_face);
    s_face = NULL;
  }
  if(s_ft) {
    FT_Done_FreeType(s_ft);
    s_ft = NULL;
  }
  if(s_font_blob) {
    free(s_font_blob);
    s_font_blob = NULL;
  }
  free(s_cells);
  s_cells  = NULL;
  s_fb     = NULL;
  s_active = 0;
}

void sh_fb_tty_clear(void)
{
  if(!s_active)
    return;
  cursor_hide_bar();
  fill_rect_px(0, 0, (int)s_inf.width, (int)s_inf.height, s_bg);
  s_tc_x      = 0;
  s_tc_y      = 0;
  s_utf8_rem  = 0;
  s_esc_state = 0;
  s_term_fg   = s_fg;
  s_term_bg   = s_bg;
  s_bold      = 0;
  s_dim       = 0;
  s_underline = 0;
  s_blink     = 0;
  s_rev       = 0;
  if(s_cells) {
    size_t n = (size_t)s_term_rows * (size_t)s_term_cols;
    for(size_t i = 0; i < n; i++) {
      s_cells[i].cp    = (uint32_t)' ';
      s_cells[i].fg    = eff_ink();
      s_cells[i].bg    = eff_bg_raw();
      s_cells[i].attrs = 0;
    }
  }
}

void sh_fb_tty_cursor_poll(void)
{
  if(!s_active)
    return;
  cursor_hide_bar();
  s_blink_show_bar = !s_blink_show_bar;
  if(s_csr_visible && s_blink_show_bar)
    cursor_draw_bar();
}

/**
 * @brief Flip the blink phase and repaint every row that contains a blink
 *        cell. Call this periodically (e.g. every ~400-500ms) from an idle
 *        hook to drive A_BLINK animation.
 *
 * Self-contained with respect to the line-edit cursor: the bar is hidden
 * before any row repaint and re-drawn at the end if cursor_poll currently
 * has it in the "on" half of its blink. This way the prompt cursor doesn't
 * flicker off for a whole tick whenever a blink cell shares the screen.
 */
void sh_fb_tty_blink_tick(void)
{
  if(!s_active || !s_cells)
    return;
  s_blink_phase = !s_blink_phase;
  cursor_hide_bar();
  for(int cy = 0; cy < s_term_rows; cy++) {
    cell_t *row = s_cells + (size_t)cy * (size_t)s_term_cols;
    int     has = 0;
    for(int cx = 0; cx < s_term_cols; cx++) {
      if(row[cx].attrs & kAttrBlink) {
        has = 1;
        break;
      }
    }
    if(has)
      redraw_line_shaped(cy);
  }
  if(s_csr_visible && s_blink_show_bar)
    cursor_draw_bar();
}

/** Clear bar pixels on framebuffer, then show bar at @c s_tc (before read or
 * after edit). */
static void cursor_sync_visible(void)
{
  if(!s_active)
    return;
  cursor_hide_bar();
  s_blink_show_bar = 1;
  if(s_csr_visible)
    cursor_draw_bar();
}

void sh_fb_tty_cursor_suspend(void)
{
  cursor_sync_visible();
}

void sh_fb_tty_cursor_after_edit(void)
{
  cursor_sync_visible();
}

void sh_fb_tty_puts(const char *s)
{
  if(!s_active || !s)
    return;
  while(*s)
    term_feed_byte((unsigned char)*s++);
}

void sh_fb_tty_putchar(unsigned char c)
{
  if(!s_active)
    return;
  term_feed_byte(c);
}

void sh_fb_tty_flush(void)
{
  if(!s_active || !s_fb)
    return;
  flush_dirty_rows();
}

void sh_fb_tty_present(void)
{
  if(!s_active || !s_fb)
    return;
  flush_dirty_rows();
  /* Under KVM, VRAM updates can lag until the next VM-exit. A yield triggers
   * a syscall that lets QEMU/KVM scan the framebuffer before the next read().
   */
  (void)sched_yield();
}
