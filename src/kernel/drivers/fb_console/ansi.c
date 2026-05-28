/**
 * @file src/kernel/drivers/fb_console/ansi.c
 * @brief ANSI/CSI escape parser, UTF-8 decoder, and SGR/colour resolution for
 * the framebuffer console.
 *
 * The entry point @ref feed_byte is the state machine: bytes coming from
 * @ref fb_console_write_raw pass through the ANSI ESC/CSI / G0-charset
 * states before falling through to UTF-8 decoding and finally
 * @ref put_cp_at_cursor. SGR colour handling lives next to the parser
 * because @c ansi16_fg / @c ansi256_to_rgb are referenced only here.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/* Catppuccin Mocha 16-colour palette. Indices match ANSI SGR 30-37 / 90-97. */
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
    if(p == SGR_RESET) {
      fb_ctx.cur_fg   = fb_ctx.default_fg;
      fb_ctx.cur_bg   = fb_ctx.default_bg;
      fb_ctx.cur_attr = 0;
    } else if(p == SGR_BOLD) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_BOLD;
    } else if(p == SGR_ITALIC) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_ITALIC;
    } else if(p == SGR_UNDERLINE) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_UNDERLINE;
    } else if(p == SGR_BLINK) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_BLINK;
    } else if(p == SGR_REVERSE) {
      fb_ctx.cur_attr |= (u8)FB_ATTR_REVERSE;
    } else if(p == SGR_NO_BOLD) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_BOLD);
    } else if(p == SGR_NO_ITALIC) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_ITALIC);
    } else if(p == SGR_NO_UNDERLINE) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_UNDERLINE);
    } else if(p == SGR_NO_BLINK) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_BLINK);
    } else if(p == SGR_NO_REVERSE) {
      fb_ctx.cur_attr = (u8)(fb_ctx.cur_attr & ~(u8)FB_ATTR_REVERSE);
    } else if(p == SGR_FG_DEFAULT) {
      fb_ctx.cur_fg = fb_ctx.default_fg;
    } else if(p == SGR_BG_DEFAULT) {
      fb_ctx.cur_bg = fb_ctx.default_bg;
    } else if(p >= SGR_FG_BASE && p <= SGR_FG_END) {
      fb_ctx.cur_fg = ansi16_fg[p - SGR_FG_BASE];
    } else if(p >= SGR_FG_BRIGHT_BASE && p <= SGR_FG_BRIGHT_END) {
      fb_ctx.cur_fg = ansi16_fg_bright[p - SGR_FG_BRIGHT_BASE];
    } else if(p >= SGR_BG_BASE && p <= SGR_BG_END) {
      fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BASE];
    } else if(p >= SGR_BG_BRIGHT_BASE && p <= SGR_BG_BRIGHT_END) {
      fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BRIGHT_BASE];
    } else if(p == SGR_FG_EXTENDED && pi + 2 < np &&
              pv[pi + 1] == SGR_EXT_FORM_256) {
      fb_ctx.cur_fg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == SGR_FG_EXTENDED && pi + 4 < np &&
              pv[pi + 1] == SGR_EXT_FORM_TRUECOLOR) {
      u32 r         = (u32)pv[pi + 2] & BYTE_MASK;
      u32 g         = (u32)pv[pi + 3] & BYTE_MASK;
      u32 b         = (u32)pv[pi + 4] & BYTE_MASK;
      fb_ctx.cur_fg = (r << 16) | (g << 8) | b;
      pi += 4;
    } else if(p == SGR_BG_EXTENDED && pi + 2 < np &&
              pv[pi + 1] == SGR_EXT_FORM_256) {
      fb_ctx.cur_bg = ansi256_to_rgb((unsigned)pv[pi + 2]);
      pi += 2;
    } else if(p == SGR_BG_EXTENDED && pi + 4 < np &&
              pv[pi + 1] == SGR_EXT_FORM_TRUECOLOR) {
      u32 r         = (u32)pv[pi + 2] & BYTE_MASK;
      u32 g         = (u32)pv[pi + 3] & BYTE_MASK;
      u32 b         = (u32)pv[pi + 4] & BYTE_MASK;
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
    if(pv[k] == DEC_PM_CURSOR_VISIBLE)
      fb_ctx.cursor_visible = (u8)on;
    else if(pv[k] == DEC_PM_APP_CURSOR_KEYS)
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
    fb_ctx.cx = (fb_ctx.cx + TAB_WIDTH) & ~TAB_SNAP_MASK;
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

void feed_byte(u8 b)
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
