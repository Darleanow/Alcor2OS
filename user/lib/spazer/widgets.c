/**
 * @file user/lib/spazer/widgets.c
 * @brief Stateless drawing primitives: label, progress, clear.
 */

#include "internal.h"

#define GLYPH_FILL  '#'
#define GLYPH_EMPTY '-'

void spz_label(WINDOW *win, int y, int x, const char *text, spz_style_t style)
{
  if(!win || !text)
    return;
  int    pair;
  chtype attr;
  spz_style_resolve(style, &pair, &attr);
  wattron(win, COLOR_PAIR(pair) | attr);
  mvwaddstr(win, y, x, text);
  wattroff(win, COLOR_PAIR(pair) | attr);
}

void spz_progress(WINDOW *win, int y, int x, int width, int num, int den)
{
  if(!win || width < 2)
    return;
  if(den <= 0)
    den = 1;
  if(num < 0)
    num = 0;
  if(num > den)
    num = den;
  int filled = (num * width) / den;

  wmove(win, y, x);
  wattron(win, COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);
  for(int i = 0; i < filled; i++)
    waddch(win, GLYPH_FILL);
  wattroff(win, COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);

  wattron(win, COLOR_PAIR(SPZ_PAIR_DIM));
  for(int i = filled; i < width; i++)
    waddch(win, GLYPH_EMPTY);
  wattroff(win, COLOR_PAIR(SPZ_PAIR_DIM));
}

void spz_clear_region(WINDOW *win, int y, int x, int rows, int cols)
{
  if(!win || rows < 1 || cols < 1)
    return;
  wattron(win, COLOR_PAIR(SPZ_PAIR_TEXT));
  for(int r = 0; r < rows; r++) {
    wmove(win, y + r, x);
    for(int c = 0; c < cols; c++)
      waddch(win, ' ');
  }
  wattroff(win, COLOR_PAIR(SPZ_PAIR_TEXT));
}
