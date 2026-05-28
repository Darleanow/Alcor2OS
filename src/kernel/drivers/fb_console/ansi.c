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

/**
 * @brief Catppuccin Mocha 16-colour foreground palette.
 *
 * Indices are the ANSI SGR codes minus the @c 30 base — slot 0 is @c SGR @c 30
 * (black), slot 7 is @c SGR @c 37 (white). Kept in-kernel rather than pushed
 * out to the spazer graphics SDK because the boot-time console must paint
 * before any userspace lib is loaded, so the palette has to live where the
 * renderer does.
 */
static const u32 ansi16_fg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};

/**
 * @brief Catppuccin Mocha bright-foreground palette (@c SGR @c 90..@c 97).
 *
 * Slot 0 is the "bright black" surface tone, used as a slightly lighter
 * background tint by some TUIs; the other slots match the non-bright palette
 * except for magenta/cyan/white which are pushed brighter so reverse-video
 * stays legible.
 */
static const u32 ansi16_fg_bright[8] = {
    0x585b70u, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xcba6f7u, 0x89dcebu, 0xa6adc8u,
};

/**
 * @brief Catppuccin Mocha 16-colour background palette (@c SGR @c 40..@c 47).
 *
 * Identical to @ref ansi16_fg because Catppuccin Mocha has no separate
 * background variants — the two arrays are kept distinct so a future palette
 * swap can tune the bg side without touching the fg renderer.
 */
static const u32 ansi16_bg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};

/** @brief First palette slot of the 6x6x6 colour cube in xterm 256. */
#define XTERM256_CUBE_BASE 16u

/** @brief First palette slot of the 24-step greyscale ramp in xterm 256. */
#define XTERM256_GREY_BASE 232u

/** @brief Number of palette slots in one slice of the 6x6x6 cube
 * (one fixed red channel = 6 greens × 6 blues). */
#define XTERM256_CUBE_SLICE 36u

/** @brief Side length of the 6x6x6 colour cube along one axis. */
#define XTERM256_CUBE_SIDE 6u

/** @brief Cube axis value 1 (out of 0..5) in the 8-bit channel space — the
 * xterm cube uses {0, 95, 135, 175, 215, 255}; this is the first non-zero
 * stop. */
#define XTERM256_CUBE_STOP_1 55u

/** @brief Spacing between successive non-zero cube stops (135-95, 175-135,
 * etc.). */
#define XTERM256_CUBE_STEP 40u

/** @brief Brightness of greyscale slot 0 (palette index 232). */
#define XTERM256_GREY_BASE_LEVEL 8u

/** @brief Step between successive greyscale slots. */
#define XTERM256_GREY_STEP 10u

/**
 * @brief Pack three 0..255 channels into a 0xRRGGBB triplet.
 *
 * Tiny helper so every place that builds an RGB word uses the named shifts
 * rather than scattering @c << @c 16 / @c << @c 8 across the renderer.
 *
 * @param r  Red channel.
 * @param g  Green channel.
 * @param b  Blue channel.
 * @return Packed 0xRRGGBB.
 */
static inline u32 rgb_pack(u32 r, u32 g, u32 b)
{
  return (r << BGRA_RED_SHIFT) | (g << BGRA_GREEN_SHIFT) | b;
}

/**
 * @brief Look up one axis (red, green, or blue) of the xterm 6x6x6 cube.
 *
 * The xterm cube uses non-linear stops @c {0, 95, 135, 175, 215, 255}; modeled
 * here as "0 for stop 0, else @c XTERM256_CUBE_STOP_1 plus a step per axis
 * tick". Pulled out of @ref ansi256_to_rgb so the cube math reads once instead
 * of three times.
 *
 * @param axis  Axis position 0..5.
 * @return 8-bit channel value at that stop.
 */
static inline u32 xterm_cube_axis(unsigned axis)
{
  if(axis == 0u)
    return 0u;
  return XTERM256_CUBE_STOP_1 + XTERM256_CUBE_STEP * (axis - 1u);
}

/**
 * @brief Map an xterm 256-colour index to a 0xRRGGBB triplet.
 *
 * Three slots back-to-back in the palette: 0..15 = base + bright ANSI,
 * 16..231 = the 6x6x6 cube, 232..255 = the 24-step greyscale. Formulas
 * mirror the xterm definitions verbatim so a TUI program sees the same
 * colours it would on real xterm.
 *
 * @param idx  Palette index 0..255.
 * @return Packed RGB.
 */
static u32 ansi256_to_rgb(unsigned idx)
{
  if(idx < SGR_FG_END - SGR_FG_BASE + 1u)
    return ansi16_fg[idx];
  if(idx < XTERM256_CUBE_BASE)
    return ansi16_fg_bright[idx - (SGR_FG_END - SGR_FG_BASE + 1u)];
  if(idx < XTERM256_GREY_BASE) {
    unsigned cube_idx = idx - XTERM256_CUBE_BASE;
    unsigned r_axis   = cube_idx / XTERM256_CUBE_SLICE;
    unsigned rem      = cube_idx % XTERM256_CUBE_SLICE;
    unsigned g_axis   = rem / XTERM256_CUBE_SIDE;
    unsigned b_axis   = rem % XTERM256_CUBE_SIDE;
    return rgb_pack(
        xterm_cube_axis(r_axis), xterm_cube_axis(g_axis),
        xterm_cube_axis(b_axis)
    );
  }
  u32 v = XTERM256_GREY_BASE_LEVEL +
          XTERM256_GREY_STEP * (idx - XTERM256_GREY_BASE);
  return rgb_pack(v, v, v);
}

/**
 * @brief Translate a DEC ACS (Special Graphics) printable ASCII byte to its
 * Unicode equivalent.
 *
 * Active only while @c ESC @c ( @c 0 has selected G0=ACS. The kernel's CP437
 * bitmap lacks most of these glyphs; the userspace atlas provides them when
 * loaded. Unrecognised bytes pass through unchanged so ncurses falling back
 * to ACS still draws plain text where a translation isn't defined.
 *
 * @param b  Input byte in the printable ASCII range.
 * @return Translated codepoint, or @p b unchanged when no mapping exists.
 */
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

/**
 * @brief Parse up to @p maxn decimal parameters from @c esc_buf into @p pv.
 *
 * Empty fields default to 0 so a sequence like @c CSI@c ;5@c m parses as
 * (0, 5) — matches xterm so SGR's "reset" semantics survive an empty
 * first param. Stops before the final byte; @ref handle_csi consumes the
 * final byte after parameter parsing.
 *
 * @param pv    Destination buffer for parsed params.
 * @param maxn  Capacity of @p pv.
 * @return Number of params parsed.
 */
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

/**
 * @brief Convenience for cursor-movement sequences: parse first param,
 * default to 1 when missing or zero.
 *
 * Matches CSI semantics for @c CUU/@c CUD/@c CUF/@c CUB and friends — a
 * missing or zero count means "move by 1", not "move by 0". Returning 1
 * directly here keeps the @ref handle_csi switch arms tiny.
 *
 * @return First parameter or 1.
 */
static int csi_param1(void)
{
  int pv[8];
  int np = csi_params(pv, 8);
  if(np == 0 || pv[0] == 0)
    return 1;
  return pv[0];
}

/**
 * @brief Blank the inclusive rectangle [(y0, x0), (y1, x1)] using the
 * current SGR fg/bg.
 *
 * Skips cells that already match the target state — every CSI @c K (erase
 * line) on a clean line would otherwise mark hundreds of unchanged cells
 * dirty and waste VRAM bandwidth. Bounds-clamp per cell rather than per
 * call so partially-off-screen rectangles still erase the visible region.
 *
 * @param y0  Top row (inclusive).
 * @param x0  Left column (inclusive).
 * @param y1  Bottom row (inclusive).
 * @param x1  Right column (inclusive).
 */
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

/**
 * @brief @c CSI @c H / @c f — cursor position. Row and column are 1-based on
 * the wire; the cell grid is 0-based, so each gets a @c -1.
 */
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

/** @brief Reset @c cur_fg / @c cur_bg / @c cur_attr to the boot defaults. */
static void sgr_reset(void)
{
  fb_ctx.cur_fg   = fb_ctx.default_fg;
  fb_ctx.cur_bg   = fb_ctx.default_bg;
  fb_ctx.cur_attr = 0;
}

/**
 * @brief Update @c cur_attr from a single-param attribute code (bold,
 *        italic, …, or the corresponding @c 21/22/23/… reset).
 *
 * @param p  SGR code already known to be in the attribute range.
 * @return @c true if @p p matched an attribute code; @c false if the caller
 *         should fall through to colour handling.
 */
static bool sgr_apply_attr(int p)
{
  switch(p) {
  case SGR_BOLD:
    fb_ctx.cur_attr |= (u8)FB_ATTR_BOLD;
    return true;
  case SGR_ITALIC:
    fb_ctx.cur_attr |= (u8)FB_ATTR_ITALIC;
    return true;
  case SGR_UNDERLINE:
    fb_ctx.cur_attr |= (u8)FB_ATTR_UNDERLINE;
    return true;
  case SGR_BLINK:
    fb_ctx.cur_attr |= (u8)FB_ATTR_BLINK;
    return true;
  case SGR_REVERSE:
    fb_ctx.cur_attr |= (u8)FB_ATTR_REVERSE;
    return true;
  case SGR_NO_BOLD:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_BOLD;
    return true;
  case SGR_NO_ITALIC:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_ITALIC;
    return true;
  case SGR_NO_UNDERLINE:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_UNDERLINE;
    return true;
  case SGR_NO_BLINK:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_BLINK;
    return true;
  case SGR_NO_REVERSE:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_REVERSE;
    return true;
  default:
    return false;
  }
}

/**
 * @brief Update @c cur_fg / @c cur_bg from a single-param colour code
 *        (default, 30..37, 40..47, 90..97, 100..107).
 *
 * @param p  SGR code.
 * @return @c true if @p p matched a basic colour code; @c false otherwise.
 */
static bool sgr_apply_basic_color(int p)
{
  if(p == SGR_FG_DEFAULT) {
    fb_ctx.cur_fg = fb_ctx.default_fg;
    return true;
  }
  if(p == SGR_BG_DEFAULT) {
    fb_ctx.cur_bg = fb_ctx.default_bg;
    return true;
  }
  if(p >= SGR_FG_BASE && p <= SGR_FG_END) {
    fb_ctx.cur_fg = ansi16_fg[p - SGR_FG_BASE];
    return true;
  }
  if(p >= SGR_FG_BRIGHT_BASE && p <= SGR_FG_BRIGHT_END) {
    fb_ctx.cur_fg = ansi16_fg_bright[p - SGR_FG_BRIGHT_BASE];
    return true;
  }
  if(p >= SGR_BG_BASE && p <= SGR_BG_END) {
    fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BASE];
    return true;
  }
  if(p >= SGR_BG_BRIGHT_BASE && p <= SGR_BG_BRIGHT_END) {
    fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BRIGHT_BASE];
    return true;
  }
  return false;
}

/**
 * @brief Handle the multi-param extended colour forms @c 38;5;N, @c 38;2;R;G;B,
 *        @c 48;5;N, @c 48;2;R;G;B.
 *
 * Advances the parameter cursor past the consumed subform so the caller can
 * resume its loop without double-counting.
 *
 * @param pv  Parameter vector.
 * @param np  Number of valid entries in @p pv.
 * @param pi  Pointer to the cursor; updated to the last consumed index.
 * @return @c true if an extended form was consumed; @c false if the params
 *         at @p pi don't form a complete subform (caller drops the code).
 */
static bool sgr_apply_extended_color(const int *pv, int np, int *pi)
{
  int p = pv[*pi];
  if(p != SGR_FG_EXTENDED && p != SGR_BG_EXTENDED)
    return false;
  if(*pi + 2 >= np)
    return false;
  int form = pv[*pi + 1];
  u32 colour;
  if(form == SGR_EXT_FORM_256) {
    colour = ansi256_to_rgb((unsigned)pv[*pi + 2]);
    *pi += 2;
  } else if(form == SGR_EXT_FORM_TRUECOLOR && *pi + 4 < np) {
    u32 r  = (u32)pv[*pi + 2] & BYTE_MASK;
    u32 g  = (u32)pv[*pi + 3] & BYTE_MASK;
    u32 b  = (u32)pv[*pi + 4] & BYTE_MASK;
    colour = (r << 16) | (g << 8) | b;
    *pi += 4;
  } else {
    return false;
  }
  if(p == SGR_FG_EXTENDED)
    fb_ctx.cur_fg = colour;
  else
    fb_ctx.cur_bg = colour;
  return true;
}

/**
 * @brief @c CSI @c m — Select Graphic Rendition: mutate fg/bg/attrs.
 *
 * Empty parameter list is the same as @c CSI @c 0 @c m (full reset). Each
 * parameter is dispatched to the attr / basic-colour / extended-colour
 * helper in turn; unknown codes are silently ignored, matching xterm.
 */
static void csi_sgr(void)
{
  int pv[32];
  int np = csi_params(pv, 32);
  if(np == 0) {
    sgr_reset();
    return;
  }
  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    if(p == SGR_RESET) {
      sgr_reset();
      continue;
    }
    if(sgr_apply_attr(p))
      continue;
    if(sgr_apply_basic_color(p))
      continue;
    (void)sgr_apply_extended_color(pv, np, &pi);
  }
}

/**
 * @brief @c CSI @c ?N @c h / @c CSI @c ?N @c l — DEC private mode set/reset.
 *
 * Only the modes the renderer actually honours are tracked: cursor
 * visibility (mode 25) and application cursor keys (mode 1). Mode @c ?1049
 * (alt screen) is intentionally ignored — we draw into the live grid and
 * accept the cosmetic mismatch in exchange for simpler state.
 *
 * @param cmd  @c 'h' for set, @c 'l' for reset.
 */
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

/**
 * @brief @c CSI @c A/@c B/@c C/@c D — relative cursor motion, clamped to the
 *        grid edge.
 *
 * One function for all four because they only differ in axis and sign; the
 * clamp logic is identical and easy to get wrong if duplicated.
 *
 * @param cmd  Final byte, one of @c A / @c B / @c C / @c D.
 */
static void csi_cursor_move(char cmd)
{
  int n = csi_param1();
  switch(cmd) {
  case 'A':
    fb_ctx.cy = (fb_ctx.cy >= n) ? (fb_ctx.cy - n) : 0;
    return;
  case 'D':
    fb_ctx.cx = (fb_ctx.cx >= n) ? (fb_ctx.cx - n) : 0;
    return;
  case 'B': {
    int mx = fb_ctx.rows - 1 - fb_ctx.cy;
    if(n > mx)
      n = mx;
    if(n > 0)
      fb_ctx.cy += n;
    return;
  }
  case 'C': {
    int mx = fb_ctx.cols - 1 - fb_ctx.cx;
    if(n > mx)
      n = mx;
    if(n > 0)
      fb_ctx.cx += n;
    return;
  }
  default:
    return;
  }
}

/**
 * @brief @c CSI @c G (CHA) / @c CSI @c d (VPA) — absolute column or row
 *        positioning. Both are 1-based on the wire and clamped to the grid.
 *
 * @param cmd  Final byte: @c G for column, @c d for row.
 */
static void csi_absolute_axis(char cmd)
{
  int n = csi_param1();
  if(cmd == 'G') {
    if(n > fb_ctx.cols)
      n = fb_ctx.cols;
    fb_ctx.cx = n - 1;
  } else { /* 'd' */
    if(n > fb_ctx.rows)
      n = fb_ctx.rows;
    fb_ctx.cy = n - 1;
  }
}

/**
 * @brief @c CSI @c J (ED) — erase in display (mode 0/1/2).
 *
 * Empty param defaults to mode 0 (cursor → end of display). Unknown modes
 * drop silently so an obscure mode 3 (scrollback clear) doesn't burn output.
 */
static void csi_erase_display(void)
{
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
}

/**
 * @brief @c CSI @c K (EL) — erase in line (mode 0/1/2).
 *
 * Same default + drop-unknown rule as @ref csi_erase_display.
 */
static void csi_erase_line(void)
{
  int pv[2];
  int np   = csi_params(pv, 2);
  int mode = (np > 0) ? pv[0] : 0;
  if(mode == 0)
    erase_rect(fb_ctx.cy, fb_ctx.cx, fb_ctx.cy, fb_ctx.cols - 1);
  else if(mode == 1)
    erase_rect(fb_ctx.cy, 0, fb_ctx.cy, fb_ctx.cx);
  else if(mode == 2)
    erase_rect(fb_ctx.cy, 0, fb_ctx.cy, fb_ctx.cols - 1);
}

/**
 * @brief @c CSI @c X (ECH) — erase Pn chars at the cursor, no cursor move.
 */
static void csi_erase_chars(void)
{
  int n  = csi_param1();
  int x1 = fb_ctx.cx + n - 1;
  if(x1 >= fb_ctx.cols)
    x1 = fb_ctx.cols - 1;
  erase_rect(fb_ctx.cy, fb_ctx.cx, fb_ctx.cy, x1);
}

/**
 * @brief @c CSI @c b (REP) — repeat the last emitted codepoint Pn times.
 *
 * No-op when no codepoint has been emitted yet — there is nothing to repeat.
 */
static void csi_repeat(void)
{
  if(fb_ctx.last_cp == 0)
    return;
  int n = csi_param1();
  for(int i = 0; i < n; i++)
    put_cp_at_cursor(fb_ctx.last_cp);
}

/** @brief @c CSI @c s — save cursor (paired with @c CSI @c u). */
static void csi_save_cursor(void)
{
  fb_ctx.saved_cx = fb_ctx.cx;
  fb_ctx.saved_cy = fb_ctx.cy;
}

/** @brief @c CSI @c u — restore cursor saved by @c CSI @c s. */
static void csi_restore_cursor(void)
{
  fb_ctx.cx = fb_ctx.saved_cx;
  fb_ctx.cy = fb_ctx.saved_cy;
}

/**
 * @brief Final-byte dispatcher for the assembled CSI sequence.
 *
 * The DEC private-mode prefix (@c ?) is detected up-front and routed to
 * @ref csi_dec_private. Each remaining final byte hands off to a small
 * per-command helper. Unknown final bytes are dropped silently — the modern
 * terminal protocol is full of obscure sequences and erroring out would
 * burn legitimate output to the screen.
 */
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
  case 'A':
  case 'B':
  case 'C':
  case 'D':
    csi_cursor_move(cmd);
    break;
  case 'H':
  case 'f':
    csi_cup();
    break;
  case 'G':
  case 'd':
    csi_absolute_axis(cmd);
    break;
  case 'J':
    csi_erase_display();
    break;
  case 'K':
    csi_erase_line();
    break;
  case 'X':
    csi_erase_chars();
    break;
  case 'm':
    csi_sgr();
    break;
  case 'b':
    csi_repeat();
    break;
  case 's':
    csi_save_cursor();
    break;
  case 'u':
    csi_restore_cursor();
    break;
  default:
    break;
  }
}

/**
 * @brief Apply one C0 control character to the cursor/grid state.
 *
 * Only the four characters userspace actually emits in normal output
 * (LF/CR/BS/HT) are honoured. Everything else (BEL, etc.) is dropped — no
 * audible terminal, no in-band signalling we care about.
 *
 * @param b  Control byte (< 0x20 or 0x7F).
 */
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

/**
 * @brief Emit a single ASCII byte to the cursor, applying DEC ACS translation
 *        when the G0 set is the special-graphics one.
 *
 * @param b  Byte in the 0x20..0x7E printable range.
 */
static void emit_ascii(u8 b)
{
  if(fb_ctx.g0_acs && b >= 0x60u) {
    put_cp_at_cursor(acs_to_unicode(b));
    return;
  }
  put_cp_at_cursor((u32)b);
}

/**
 * @brief Begin a multi-byte UTF-8 sequence: store the lead bits and remaining
 *        continuation-byte count, then wait for more input.
 *
 * @param lead_bits   Payload bits carried by the lead byte.
 * @param remaining   Continuation bytes still expected.
 */
static void utf8_begin(u32 lead_bits, u8 remaining)
{
  fb_ctx.utf8_partial = lead_bits;
  fb_ctx.utf8_rem     = remaining;
}

/**
 * @brief Try to start a UTF-8 multi-byte sequence based on the high bits of
 *        @p b.
 *
 * @param b  Lead byte (≥ 0x80).
 * @return @c true if @p b is a valid 2/3/4-byte lead and the decoder is now
 *         armed; @c false if @p b is a stray continuation or invalid lead.
 */
static bool utf8_try_start(u8 b)
{
  if((b & 0xe0u) == 0xc0u) {
    utf8_begin((u32)(b & 0x1fu), 1);
    return true;
  }
  if((b & 0xf0u) == 0xe0u) {
    utf8_begin((u32)(b & 0x0fu), 2);
    return true;
  }
  if((b & 0xf8u) == 0xf0u) {
    utf8_begin((u32)(b & 0x07u), 3);
    return true;
  }
  return false;
}

/**
 * @brief Stream one byte through the UTF-8 decoder; emit a codepoint when a
 *        sequence completes.
 *
 * Self-restarting on broken sequences (stray continuation, invalid lead):
 * emits a @c ? and replays the offending byte fresh. This keeps a corrupt
 * input stream from desyncing the parser forever, at the cost of one
 * placeholder glyph. ANSI/CSI sequences are stripped upstream by
 * @ref feed_byte so this only ever sees printable / control bytes.
 *
 * @param b  Input byte.
 */
static void feed_utf8(u8 b)
{
  if(fb_ctx.utf8_rem == 0) {
    if(b < 0x20u || b == 0x7fu) {
      handle_control(b);
      return;
    }
    if(b < 0x80u) {
      emit_ascii(b);
      return;
    }
    if(!utf8_try_start(b))
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
    put_cp_at_cursor((cp <= 0x10ffffu) ? cp : (u32)'?');
  }
}

/**
 * @brief Top-level byte sink: drive the ESC/CSI state machine, fall through
 * to UTF-8 on plain bytes.
 *
 * Four-state machine: 0 = normal, 1 = saw @c ESC waiting for next, 2 =
 * inside CSI accumulating parameters, 3 = inside @c ESC@c (/@c ) charset
 * designator. Unknown @c ESC@c <byte> sequences are dropped to state 0 so
 * a stray escape doesn't poison subsequent output.
 *
 * @param b  Input byte.
 */
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
