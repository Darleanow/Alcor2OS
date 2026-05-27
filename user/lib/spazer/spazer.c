#include <spazer/spazer.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_FILL  '#'
#define GLYPH_EMPTY '-'
#define GLYPH_ARROW '>'

void spz_init(void)
{
  if(!has_colors())
    return;
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

/* ACS_* values are populated at runtime by ncurses (not compile-time
 * constants). waddch with ACS_* is the only portable way in standard non-wide
 * ncurses — waddstr with UTF-8 box chars outputs each byte as a separate cell.
 */
static void draw_border(
    WINDOW *win, int rows, int cols, const char *title, int border_style
)
{
  if(border_style < 0 || border_style > SPZ_BORDER_HEAVY)
    border_style = SPZ_BORDER_SINGLE;
  /* DOUBLE and HEAVY share ACS glyphs; A_BOLD gives them visual weight. */
  chtype extra = (border_style != SPZ_BORDER_SINGLE) ? A_BOLD : 0;

  wattron(win, COLOR_PAIR(SPZ_PAIR_BORDER) | extra);

  mvwaddch(win, 0, 0, ACS_ULCORNER);
  for(int i = 1; i < cols - 1; i++)
    waddch(win, ACS_HLINE);
  waddch(win, ACS_URCORNER);

  for(int r = 1; r < rows - 1; r++) {
    mvwaddch(win, r, 0, ACS_VLINE);
    wattroff(win, COLOR_PAIR(SPZ_PAIR_BORDER) | extra);
    wattron(win, COLOR_PAIR(SPZ_PAIR_TEXT));
    for(int c = 1; c < cols - 1; c++)
      waddch(win, ' ');
    wattroff(win, COLOR_PAIR(SPZ_PAIR_TEXT));
    wattron(win, COLOR_PAIR(SPZ_PAIR_BORDER) | extra);
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

spz_panel_t *spz_panel_new(
    int y, int x, int rows, int cols, const char *title, int border_style
)
{
  if(rows < 3 || cols < 4)
    return NULL;

  spz_panel_t *p = (spz_panel_t *)malloc(sizeof(spz_panel_t));
  if(!p)
    return NULL;

  p->y            = y;
  p->x            = x;
  p->rows         = rows;
  p->cols         = cols;
  p->border_style = border_style;

  p->frame = newwin(rows, cols, y, x);
  if(!p->frame) {
    free(p);
    return NULL;
  }

  wbkgd(p->frame, COLOR_PAIR(SPZ_PAIR_TEXT));
  werase(p->frame);
  draw_border(p->frame, rows, cols, title, border_style);

  p->body = derwin(p->frame, rows - 2, cols - 2, 1, 1);
  if(!p->body) {
    delwin(p->frame);
    free(p);
    return NULL;
  }

  wbkgd(p->body, COLOR_PAIR(SPZ_PAIR_TEXT));
  werase(p->body);

  return p;
}

void spz_panel_refresh(spz_panel_t *p)
{
  if(!p)
    return;
  wnoutrefresh(p->frame);
  wnoutrefresh(p->body);
  doupdate();
}

void spz_panel_redraw(spz_panel_t *p, const char *title)
{
  if(!p)
    return;
  werase(p->frame);
  draw_border(p->frame, p->rows, p->cols, title, p->border_style);
  werase(p->body);
}

void spz_panel_del(spz_panel_t *p)
{
  if(!p)
    return;
  if(p->body)
    delwin(p->body);
  if(p->frame)
    delwin(p->frame);
  free(p);
}

void spz_label(WINDOW *win, int y, int x, const char *text, int style)
{
  if(!win || !text)
    return;
  int pair, attr;
  switch(style) {
  case SPZ_STYLE_TITLE:
    pair = SPZ_PAIR_TITLE;
    attr = A_BOLD;
    break;
  case SPZ_STYLE_ACCENT:
    pair = SPZ_PAIR_ACCENT;
    attr = A_BOLD;
    break;
  case SPZ_STYLE_DIM:
    pair = SPZ_PAIR_DIM;
    attr = 0;
    break;
  case SPZ_STYLE_SELECT:
    pair = SPZ_PAIR_SELECT;
    attr = A_BOLD;
    break;
  default:
    pair = SPZ_PAIR_TEXT;
    attr = 0;
    break;
  }
  wattron(win, COLOR_PAIR(pair) | attr);
  mvwaddstr(win, y, x, text);
  wattroff(win, COLOR_PAIR(pair) | attr);
}

void spz_clear_region(WINDOW *win, int y, int x, int rows, int cols)
{
  if(!win)
    return;
  wattron(win, COLOR_PAIR(SPZ_PAIR_TEXT));
  for(int r = 0; r < rows; r++) {
    wmove(win, y + r, x);
    for(int c = 0; c < cols; c++)
      waddch(win, ' ');
  }
  wattroff(win, COLOR_PAIR(SPZ_PAIR_TEXT));
}

void spz_progress(WINDOW *win, int y, int x, int width, int num, int den)
{
  if(!win || width < 2 || den <= 0)
    return;
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

void spz_statusbar(const char *left, const char *center, const char *right)
{
  int rows = LINES;
  int cols = COLS;
  if(rows < 1 || cols < 1)
    return;

  WINDOW *bar = newwin(1, cols, rows - 1, 0);
  if(!bar)
    return;

  wbkgd(bar, COLOR_PAIR(SPZ_PAIR_STATUS) | A_BOLD);
  werase(bar);
  wattron(bar, COLOR_PAIR(SPZ_PAIR_STATUS) | A_BOLD);

  if(left)
    mvwaddstr(bar, 0, 1, left);

  if(center) {
    int clen = (int)strlen(center);
    int cx   = (cols - clen) / 2;
    if(cx > 0)
      mvwaddstr(bar, 0, cx, center);
  }

  if(right) {
    int rlen = (int)strlen(right);
    int rx   = cols - rlen - 1;
    if(rx > 0)
      mvwaddstr(bar, 0, rx, right);
  }

  wattroff(bar, COLOR_PAIR(SPZ_PAIR_STATUS) | A_BOLD);
  wrefresh(bar);
  delwin(bar);
}

int spz_menu(int y, int x, const char *const *items, int n, const char *title)
{
  if(!items || n <= 0)
    return -1;

  int max_w = title ? (int)strlen(title) : 0;
  for(int i = 0; i < n; i++) {
    int l = items[i] ? (int)strlen(items[i]) : 0;
    if(l > max_w)
      max_w = l;
  }
  int          inner_w = max_w + 4;
  int          cols    = inner_w + 2;
  int          rows    = n + 2;

  spz_panel_t *p = spz_panel_new(y, x, rows, cols, title, SPZ_BORDER_DOUBLE);
  if(!p)
    return -1;

  keypad(p->body, TRUE);
  notimeout(p->body, TRUE);

  int sel = 0;
  for(;;) {
    for(int i = 0; i < n; i++) {
      const char *item = items[i] ? items[i] : "";
      int         ilen = (int)strlen(item);

      if(i == sel) {
        wattron(p->body, COLOR_PAIR(SPZ_PAIR_SELECT) | A_BOLD);
        wmove(p->body, i, 0);
        waddch(p->body, GLYPH_ARROW);
        waddch(p->body, ' ');
        waddstr(p->body, item);
        for(int sp = ilen + 2; sp < inner_w; sp++)
          waddch(p->body, ' ');
        wattroff(p->body, COLOR_PAIR(SPZ_PAIR_SELECT) | A_BOLD);
      } else {
        wattron(p->body, COLOR_PAIR(SPZ_PAIR_TEXT));
        wmove(p->body, i, 0);
        waddch(p->body, ' ');
        waddch(p->body, ' ');
        waddstr(p->body, item);
        for(int sp = ilen + 2; sp < inner_w; sp++)
          waddch(p->body, ' ');
        wattroff(p->body, COLOR_PAIR(SPZ_PAIR_TEXT));
      }
    }
    spz_panel_refresh(p);

    int ch = wgetch(p->body);
    switch(ch) {
    case KEY_UP:
    case 'k':
      if(sel > 0)
        sel--;
      break;
    case KEY_DOWN:
    case 'j':
      if(sel < n - 1)
        sel++;
      break;
    case '\n':
    case '\r':
    case KEY_ENTER:
      spz_panel_del(p);
      return sel;
    case 27:
    case 'q':
      spz_panel_del(p);
      return -1;
    default:
      break;
    }
  }
}
