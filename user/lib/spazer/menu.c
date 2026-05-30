/**
 * @file user/lib/spazer/menu.c
 * @brief Modal list selector built on top of a panel.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define GLYPH_ARROW '>'

/**
 * @brief Paint one row of the menu inside the panel body.
 *
 * @param body     Body window.
 * @param row      Row index in the body (0-based).
 * @param item     Item text, NULL replaced with empty.
 * @param inner_w  Width of the body row to pad with trailing spaces.
 * @param selected Non-zero if this row is highlighted.
 */
static void paint_row(
    WINDOW *body, int row, const char *item, int inner_w, bool selected
)
{
  const char *txt  = item ? item : "";
  int         tlen = (int)strlen(txt);

  int         pair  = selected ? SPZ_PAIR_SELECT : SPZ_PAIR_TEXT;
  chtype      attrs = selected ? A_BOLD : 0;

  wattron(body, COLOR_PAIR(pair) | attrs);
  wmove(body, row, 0);
  waddch(body, selected ? GLYPH_ARROW : ' ');
  waddch(body, ' ');
  waddstr(body, txt);
  for(int x = tlen + 2; x < inner_w; x++)
    waddch(body, ' ');
  wattroff(body, COLOR_PAIR(pair) | attrs);
}

/**
 * @brief Compute the body size needed to host every menu item.
 *
 * @param m        Menu spec.
 * @param out_w    Out: required inner width.
 * @param out_h    Out: required inner height.
 */
static void measure(const spz_menu_t *m, int *out_w, int *out_h)
{
  int max_w = m->title ? (int)strlen(m->title) : 0;
  for(int i = 0; i < m->n_items; i++) {
    int l = m->items[i] ? (int)strlen(m->items[i]) : 0;
    if(l > max_w)
      max_w = l;
  }
  *out_w = max_w + 4;
  *out_h = m->n_items;
}

int spz_menu_run(const spz_menu_t *m)
{
  if(!m || !m->items || m->n_items <= 0)
    return -1;

  int inner_w, inner_h;
  measure(m, &inner_w, &inner_h);
  spz_rect_t r = m->rect;
  r.rows       = inner_h + 2;
  r.cols       = inner_w + 2;

  spz_panel_t *p = spz_panel_new(r, m->title, SPZ_BORDER_DOUBLE);
  if(!p)
    return -1;
  WINDOW *body = spz_panel_body(p);
  keypad(body, TRUE);
  notimeout(body, TRUE);
  int prev_curs = curs_set(0);

  int sel = (m->initial < 0)             ? 0
            : (m->initial >= m->n_items) ? m->n_items - 1
                                         : m->initial;

  for(;;) {
    for(int i = 0; i < m->n_items; i++)
      paint_row(body, i, m->items[i], inner_w, i == sel);
    spz_panel_refresh(p);

    int ch = wgetch(body);
    switch(ch) {
    case KEY_UP:
    case 'k':
      if(sel > 0)
        sel--;
      break;
    case KEY_DOWN:
    case 'j':
      if(sel < m->n_items - 1)
        sel++;
      break;
    case '\n':
    case '\r':
    case KEY_ENTER:
      curs_set(prev_curs);
      spz_panel_del(p);
      return sel;
    case 27:
    case 'q':
      curs_set(prev_curs);
      spz_panel_del(p);
      return -1;
    default:
      break;
    }
  }
}
