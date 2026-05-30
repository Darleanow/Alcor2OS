/**
 * @file src/kernel/drivers/fb_console/csi.c
 * @brief @c CSI sequence parser and dispatcher.
 *
 * Owns the @c CSI param parser shared with @c sgr.c, the per-final-byte
 * command helpers (cursor movement, erase, save/restore, REP, DEC private
 * modes), and the @ref handle_csi switch that drives them. The complementary
 * UTF-8 / ESC state machine and SGR colour handling live in @c ansi.c and
 * @c sgr.c respectively.
 */

#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Parse up to @p maxn decimal parameters from @c esc_buf into @p pv.
 *
 * Empty fields default to 0 so a sequence like @c CSI@c ;5@c m parses as
 * (0, 5) — matches xterm so SGR's "reset" semantics survive an empty first
 * param. Stops before the final byte; @ref handle_csi consumes it after.
 *
 * @param pv    Destination buffer for parsed params.
 * @param maxn  Capacity of @p pv.
 * @return Number of params parsed.
 */
int csi_params(int *pv, int maxn)
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
 * @brief Blank the inclusive rectangle [(y0, x0), (y1, x1)] using the current
 * SGR fg/bg.
 *
 * Skips cells that already match the target state — every CSI @c K (erase
 * line) on a clean line would otherwise mark hundreds of unchanged cells
 * dirty and waste VRAM bandwidth. Bounds-clamp per cell rather than per call
 * so partially-off-screen rectangles still erase the visible region.
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
      c->cp        = (u32)' ';
      c->fg        = fb_ctx.cur_fg;
      c->bg        = fb_ctx.cur_bg;
      c->attr      = 0;
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

/** @brief Capacity of the DEC private-mode param vector. xterm only ever
 * sends a handful of modes in one sequence; 4 covers every real-world case
 * without growing the kernel stack. */
#define DEC_PM_MAX_PARAMS 4

/** @brief @c esc_buf length minimum for a valid private-mode sequence:
 * @c ? + at least one digit + final byte. Anything shorter is malformed. */
#define DEC_PM_MIN_LEN 3

/**
 * @brief Parse the DEC private-mode param list out of @c esc_buf.
 *
 * @c esc_buf starts with @c ? followed by semicolon-separated decimals and
 * then the final byte. Skips empty params (one stray @c ; doesn't add a
 * zero), which matches xterm's quiet handling of malformed sequences.
 *
 * @param pv  Destination buffer, sized to @ref DEC_PM_MAX_PARAMS.
 * @return Number of params parsed.
 */
static int dec_pm_parse_params(int *pv)
{
  int np = 0;
  int i  = 1;
  while(i < fb_ctx.esc_len - 1 && np < DEC_PM_MAX_PARAMS) {
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
  return np;
}

/**
 * @brief Apply one parsed DEC private-mode number with set/reset polarity.
 *
 * Only the modes the renderer honours are tracked: cursor visibility (25) and
 * application cursor keys (1). Mode @c ?1049 (alt screen) is intentionally
 * ignored — we draw into the live grid and accept the cosmetic mismatch in
 * exchange for simpler state.
 *
 * @param mode  DEC private-mode number.
 * @param on    @c true for @c h (set), @c false for @c l (reset).
 */
static void dec_pm_apply(int mode, bool on)
{
  if(mode == DEC_PM_CURSOR_VISIBLE)
    fb_ctx.cursor_visible = (u8)(on ? 1u : 0u);
  else if(mode == DEC_PM_APP_CURSOR_KEYS)
    fb_ctx.app_cursor_keys = on;
}

/**
 * @brief @c CSI @c ?N @c h / @c CSI @c ?N @c l — DEC private mode set/reset.
 *
 * Parse-then-apply split: @ref dec_pm_parse_params extracts the param list,
 * @ref dec_pm_apply consumes one entry. Done this way so the parser does not
 * need to know which modes the renderer cares about, and the apply step does
 * not need to know how the param list was buffered.
 *
 * @param cmd  @c 'h' for set, @c 'l' for reset.
 */
static void csi_dec_private(char cmd)
{
  if(fb_ctx.esc_len < DEC_PM_MIN_LEN)
    return;
  int pv[DEC_PM_MAX_PARAMS];
  int np = dec_pm_parse_params(pv);
  for(int k = 0; k < np; k++)
    dec_pm_apply(pv[k], cmd == 'h');
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
 * @brief @c CSI @c r (DECSTBM) — set the top + bottom margins of the
 * scrolling region.
 *
 * Empty params reset to "full screen" (top=0, bot=rows-1). On the wire the
 * params are 1-based and inclusive: @c CSI@c 2;23r means rows 2..23 in
 * 1-based — internally we store them 0-based, so 1..22 in cell coords.
 *
 * Out-of-range or swapped values silently clamp to the grid so a runaway
 * @c stty rows can't desync the parser. The cursor is moved to @c (0, 0)
 * (origin mode), matching xterm behaviour.
 */
static void csi_set_scroll_region(void)
{
  int pv[2];
  int np  = csi_params(pv, 2);
  int top = (np > 0 && pv[0] > 0) ? pv[0] - 1 : 0;
  int bot = (np > 1 && pv[1] > 0) ? pv[1] - 1 : fb_ctx.rows - 1;

  if(top < 0)
    top = 0;
  if(bot >= fb_ctx.rows)
    bot = fb_ctx.rows - 1;
  if(top >= bot) {
    /* Invalid region — fall back to full screen rather than wedge. */
    top = 0;
    bot = fb_ctx.rows - 1;
  }

  fb_ctx.scroll_top = top;
  fb_ctx.scroll_bot = bot;
  fb_ctx.cx         = 0;
  fb_ctx.cy         = 0;
}

/**
 * @brief Grow the batch row range to cover @c [r0, r1] and mark every cell in
 * those rows dirty.
 *
 * Used by IL / DL after shifting cell rows so the next @ref flush_batch
 * repaints the moved region — the cells were already changed by the shift but
 * @c commit_cell_at-style dirty bookkeeping was bypassed.
 */
static void mark_rows_dirty(int r0, int r1)
{
  if(r0 < 0)
    r0 = 0;
  if(r1 >= fb_ctx.rows)
    r1 = fb_ctx.rows - 1;
  for(int y = r0; y <= r1; y++) {
    fb_cell_t *row = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols];
    for(int x = 0; x < fb_ctx.cols; x++)
      row[x].dirty = 1;
  }
  if(fb_ctx.in_batch) {
    if(r0 < fb_ctx.batch_r0)
      fb_ctx.batch_r0 = r0;
    if(r1 > fb_ctx.batch_r1)
      fb_ctx.batch_r1 = r1;
  }
}

/**
 * @brief Scroll the DECSTBM region down by one row.
 *
 * Shared by @c ESC@c M (RI) and any future scroll-on-RI logic. The shift is
 * bottom-up so source rows are read before overwrite. Cells outside the
 * region are untouched.
 */
void scroll_region_down(void)
{
  int top = fb_ctx.scroll_top;
  int bot = fb_ctx.scroll_bot;
  if(top < 0 || bot >= fb_ctx.rows || top >= bot)
    return;

  for(int y = bot; y > top; y--) {
    fb_cell_t *dst = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols];
    fb_cell_t *src = &fb_ctx.cells[(size_t)(y - 1) * (size_t)fb_ctx.cols];
    kmemcpy(dst, src, (size_t)fb_ctx.cols * sizeof(fb_cell_t));
  }
  erase_rect(top, 0, top, fb_ctx.cols - 1);
  if(fb_ctx.in_batch) {
    mark_rows_dirty(top + 1, bot);
  } else {
    for(int y = top + 1; y <= bot; y++)
      for(int x = 0; x < fb_ctx.cols; x++)
        blit_cell(x, y);
  }
}

/**
 * @brief @c CSI @c L (IL) — insert Pn blank lines at the cursor row.
 *
 * Operates strictly inside the DECSTBM region: rows @c [cy, scroll_bot-n] are
 * shifted down by @p n, the @p n rows starting at @c cy are blanked with the
 * current SGR background, and rows that would slide past @c scroll_bot are
 * discarded. A no-op when the cursor sits outside the region — matches xterm.
 *
 * Without this, ncurses' line-insert optimisation (used after a line split in
 * an editor) emits the sequence and the kernel silently dropped it, leaving
 * the screen out of sync with the application's idea of the grid.
 */
static void csi_insert_lines(void)
{
  int top = fb_ctx.scroll_top;
  int bot = fb_ctx.scroll_bot;
  if(fb_ctx.cy < top || fb_ctx.cy > bot)
    return;

  int n = csi_param1();
  if(n < 1)
    return;
  int max = bot - fb_ctx.cy + 1;
  if(n > max)
    n = max;

  /* Shift rows [cy, bot-n] down to [cy+n, bot]. Iterate from bottom up so we
   * don't overwrite source rows before they have been copied. */
  for(int y = bot; y >= fb_ctx.cy + n; y--) {
    fb_cell_t *dst = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols];
    fb_cell_t *src = &fb_ctx.cells[(size_t)(y - n) * (size_t)fb_ctx.cols];
    kmemcpy(dst, src, (size_t)fb_ctx.cols * sizeof(fb_cell_t));
  }
  /* Blank the n rows the insert opened up at the cursor. erase_rect already
   * grows batch_r0/r1 for the cleared cells. */
  erase_rect(fb_ctx.cy, 0, fb_ctx.cy + n - 1, fb_ctx.cols - 1);
  /* The shifted rows changed too: in batch mode mark them dirty so the next
   * flush_batch repaints them; otherwise blit the moved cells right now. */
  if(fb_ctx.in_batch) {
    mark_rows_dirty(fb_ctx.cy + n, bot);
  } else {
    for(int y = fb_ctx.cy + n; y <= bot; y++)
      for(int x = 0; x < fb_ctx.cols; x++)
        blit_cell(x, y);
  }
}

/**
 * @brief @c CSI @c M (DL) — delete Pn lines at the cursor row.
 *
 * Operates strictly inside the DECSTBM region: rows @c [cy+n, scroll_bot] are
 * shifted up to @c [cy, scroll_bot-n], and the @p n rows at the bottom of the
 * region are blanked with the current SGR background. A no-op when the cursor
 * sits outside the region. Symmetric of @ref csi_insert_lines.
 */
static void csi_delete_lines(void)
{
  int top = fb_ctx.scroll_top;
  int bot = fb_ctx.scroll_bot;
  if(fb_ctx.cy < top || fb_ctx.cy > bot)
    return;

  int n = csi_param1();
  if(n < 1)
    return;
  int max = bot - fb_ctx.cy + 1;
  if(n > max)
    n = max;

  /* Shift rows [cy+n, bot] up to [cy, bot-n]. Top-down iteration is safe
   * here since src is always below dst. */
  for(int y = fb_ctx.cy; y <= bot - n; y++) {
    fb_cell_t *dst = &fb_ctx.cells[(size_t)y * (size_t)fb_ctx.cols];
    fb_cell_t *src = &fb_ctx.cells[(size_t)(y + n) * (size_t)fb_ctx.cols];
    kmemcpy(dst, src, (size_t)fb_ctx.cols * sizeof(fb_cell_t));
  }
  /* Blank the n rows that the delete vacated at the bottom of the region. */
  erase_rect(bot - n + 1, 0, bot, fb_ctx.cols - 1);
  /* The shifted rows changed too: in batch mode mark them dirty so the next
   * flush_batch repaints them; otherwise blit the moved cells right now. */
  if(fb_ctx.in_batch) {
    mark_rows_dirty(fb_ctx.cy, bot - n);
  } else {
    for(int y = fb_ctx.cy; y <= bot - n; y++)
      for(int x = 0; x < fb_ctx.cols; x++)
        blit_cell(x, y);
  }
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
void handle_csi(void)
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
  case 'L':
    csi_insert_lines();
    break;
  case 'M':
    csi_delete_lines();
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
  case 'r':
    csi_set_scroll_region();
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
