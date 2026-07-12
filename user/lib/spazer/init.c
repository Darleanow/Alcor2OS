/**
 * @file user/lib/spazer/init.c
 * @brief Library init / shutdown, palette registration, style resolution,
 *        and the shared border-drawing helper.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief One-shot guard so a second @ref spz_init returns success without
 *        re-running the ncurses bring-up (which would corrupt state).
 */
static bool s_initialised = false;

int         spz_init(void)
{
  if(s_initialised)
    return 0;

  if(!stdscr)
    return -1;

  raw();
  noecho();
  keypad(stdscr, TRUE);

  if(has_colors()) {
    start_color();
    use_default_colors();
    init_pair(SPZ_PAIR_BORDER, THEME_COL_PRIMARY, -1);
    init_pair(SPZ_PAIR_TITLE, THEME_COL_ACCENT, -1);
    init_pair(SPZ_PAIR_TEXT, THEME_COL_TEXT, -1);
    init_pair(SPZ_PAIR_ACCENT, THEME_COL_SUCCESS, -1);
    init_pair(SPZ_PAIR_SELECT, -1, THEME_COL_PRIMARY);
    init_pair(SPZ_PAIR_DIM, THEME_COL_DIM, -1);
    init_pair(SPZ_PAIR_STATUS, -1, THEME_COL_ACCENT);
  }

  s_initialised = true;
  return 0;
}

void spz_shutdown(void)
{
  if(!s_initialised)
    return;
  s_initialised = false;
}

void spz_style_resolve(spz_style_t style, int *pair, chtype *attr)
{
  switch(style) {
  case SPZ_STYLE_TITLE:
    *pair = SPZ_PAIR_TITLE;
    *attr = A_BOLD;
    return;
  case SPZ_STYLE_ACCENT:
    *pair = SPZ_PAIR_ACCENT;
    *attr = A_BOLD;
    return;
  case SPZ_STYLE_DIM:
    *pair = SPZ_PAIR_DIM;
    *attr = 0;
    return;
  case SPZ_STYLE_SELECT:
    *pair = SPZ_PAIR_SELECT;
    *attr = A_BOLD;
    return;
  case SPZ_STYLE_STATUS:
    *pair = SPZ_PAIR_STATUS;
    *attr = A_BOLD;
    return;
  case SPZ_STYLE_NORMAL:
  default:
    *pair = SPZ_PAIR_TEXT;
    *attr = 0;
    return;
  }
}

void spz_border_draw(
    WINDOW *win, int rows, int cols, const char *title, spz_border_t style
)
{
  if(!win || rows < 2 || cols < 2)
    return;
  if(style < SPZ_BORDER_NONE || style > SPZ_BORDER_HEAVY)
    style = SPZ_BORDER_SINGLE;
  if(style == SPZ_BORDER_NONE)
    return;

  /* Double and heavy share the ACS glyph set in non-wide ncurses; A_BOLD
   * gives them visual weight to differentiate from single. */
  chtype extra = (style != SPZ_BORDER_SINGLE) ? A_BOLD : 0;

  wattron(win, COLOR_PAIR(SPZ_PAIR_BORDER) | extra);

  mvwaddch(win, 0, 0, ACS_ULCORNER);
  for(int i = 1; i < cols - 1; i++)
    waddch(win, ACS_HLINE);
  waddch(win, ACS_URCORNER);

  for(int r = 1; r < rows - 1; r++) {
    mvwaddch(win, r, 0, ACS_VLINE);
    mvwaddch(win, r, cols - 1, ACS_VLINE);
  }

  mvwaddch(win, rows - 1, 0, ACS_LLCORNER);
  for(int i = 1; i < cols - 1; i++)
    waddch(win, ACS_HLINE);
  waddch(win, ACS_LRCORNER);

  wattroff(win, COLOR_PAIR(SPZ_PAIR_BORDER) | extra);

  if(title && *title) {
    int tlen = (int)strlen(title);
    int tx   = (cols - tlen - 4) / 2;
    if(tx < 1)
      tx = 1;
    if(tx + tlen + 4 < cols) {
      wattron(win, COLOR_PAIR(SPZ_PAIR_TITLE) | A_BOLD);
      mvwaddch(win, 0, tx, '[');
      waddch(win, ' ');
      waddstr(win, title);
      waddch(win, ' ');
      waddch(win, ']');
      wattroff(win, COLOR_PAIR(SPZ_PAIR_TITLE) | A_BOLD);
    }
  }
}

char *spz_strdup(const char *s)
{
  if(!s)
    return NULL;
  size_t n = strlen(s);
  char  *d = (char *)malloc(n + 1);
  if(!d)
    return NULL;
  memcpy(d, s, n + 1);
  return d;
}

int spz_clamp(int val, int max)
{
  if(max < 0)
    return 0;
  if(val < 0)
    return 0;
  if(val > max)
    return max;
  return val;
}

spz_rect_t spz_clamp_to_screen(spz_rect_t r)
{
  int max_rows = LINES;
  int max_cols = COLS;
  if(r.y < 0)
    r.y = 0;
  if(r.x < 0)
    r.x = 0;
  if(r.y + r.rows > max_rows)
    r.rows = max_rows - r.y;
  if(r.x + r.cols > max_cols)
    r.cols = max_cols - r.x;
  return r;
}
