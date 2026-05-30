/**
 * @file user/lib/spazer/bar.c
 * @brief Top / bottom one-row bars (status, header).
 */

#include "internal.h"

#include <string.h>

/**
 * @brief Paint one slot of text in @p win at @p (0, x), respecting the
 *        background attribute already set on @p win.
 *
 * @param win   Target bar window.
 * @param x     Left column of the slot.
 * @param text  UTF-8 text. NULL is a no-op.
 */
static void put_slot(WINDOW *win, int x, const char *text)
{
  if(!text || x < 0)
    return;
  mvwaddstr(win, 0, x, text);
}

/**
 * @brief Render one one-row bar at the given screen row.
 *
 * Allocates a transient @c newwin for the bar, paints left/center/right
 * slots in the resolved style, and refreshes immediately. The transient
 * window pattern matches the original spazer behaviour and avoids growing
 * stdscr's state for a bar that lives one frame.
 *
 * @param row    Screen row to paint on. @c -1 for "bottom".
 * @param bar    Bar spec.
 * @param fb_st  Style to use when @c bar->style is @c SPZ_STYLE_NORMAL.
 */
static void render_bar(int row, const spz_bar_t *bar, spz_style_t fb_st)
{
  if(!bar)
    return;
  int cols = COLS;
  int rows = LINES;
  if(rows < 1 || cols < 1)
    return;
  if(row < 0)
    row = rows - 1;
  if(row >= rows)
    return;

  spz_style_t st = (bar->style == SPZ_STYLE_NORMAL) ? fb_st : bar->style;
  int         pair;
  chtype      attr;
  spz_style_resolve(st, &pair, &attr);

  WINDOW *w = newwin(1, cols, row, 0);
  if(!w)
    return;

  wbkgd(w, COLOR_PAIR(pair) | attr);
  werase(w);
  wattron(w, COLOR_PAIR(pair) | attr);

  put_slot(w, 1, bar->left);

  if(bar->center) {
    int clen = (int)strlen(bar->center);
    int cx   = (cols - clen) / 2;
    if(cx > 0)
      put_slot(w, cx, bar->center);
  }

  if(bar->right) {
    int rlen = (int)strlen(bar->right);
    int rx   = cols - rlen - 1;
    if(rx > 0)
      put_slot(w, rx, bar->right);
  }

  wattroff(w, COLOR_PAIR(pair) | attr);
  wnoutrefresh(w);
  doupdate();
  delwin(w);
}

void spz_statusbar(const spz_bar_t *bar)
{
  render_bar(-1, bar, SPZ_STYLE_STATUS);
}

void spz_headerbar(const spz_bar_t *bar)
{
  render_bar(0, bar, SPZ_STYLE_TITLE);
}
