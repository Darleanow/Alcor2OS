/**
 * @file user/lib/spazer/panel.c
 * @brief Framed panel: outer frame window + derived body window.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Concrete layout of a panel handle.
 *
 * Two ncurses windows: @c frame holds the border + title and owns @c body as
 * a subwindow (@c derwin). @c title is owned by the panel and freed on
 * @ref spz_panel_del.
 */
struct spz_panel {
  WINDOW      *frame;
  WINDOW      *body;
  spz_rect_t   rect;
  spz_border_t border;
  char        *title; /* heap-owned copy, NULL for none */
  bool         frame_dirty;
};

/**
 * @brief Clamp @p r to the screen so out-of-bounds geometry never crashes
 *        ncurses with a NULL @c newwin.
 *
 * @param r  Rectangle to clamp.
 * @return   The clamped rectangle.
 */
static spz_rect_t clamp_to_screen(spz_rect_t r)
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

/**
 * @brief Duplicate a title into heap memory, or return NULL.
 *
 * Centralised so @ref spz_panel_new and @ref spz_panel_set_title share the
 * same allocation policy.
 *
 * @param title  Source string, NULL allowed.
 * @return       Owned copy or NULL.
 */
static char *dup_title(const char *title)
{
  if(!title)
    return NULL;
  size_t n = strlen(title);
  char  *d = (char *)malloc(n + 1);
  if(!d)
    return NULL;
  memcpy(d, title, n + 1);
  return d;
}

spz_panel_t *spz_panel_new(spz_rect_t r, const char *title, spz_border_t b)
{
  r = clamp_to_screen(r);
  if(r.rows < 1 || r.cols < 1)
    return NULL;
  /* A bordered panel needs at least 3x3 so there's room for body. */
  bool has_border = (b != SPZ_BORDER_NONE);
  if(has_border && (r.rows < 3 || r.cols < 3))
    return NULL;

  spz_panel_t *p = (spz_panel_t *)calloc(1, sizeof(*p));
  if(!p)
    return NULL;

  p->rect        = r;
  p->border      = b;
  p->title       = dup_title(title);
  p->frame_dirty = true;

  p->frame       = newwin(r.rows, r.cols, r.y, r.x);
  if(!p->frame) {
    free(p->title);
    free(p);
    return NULL;
  }
  wbkgd(p->frame, COLOR_PAIR(SPZ_PAIR_TEXT));

  if(has_border) {
    p->body = derwin(p->frame, r.rows - 2, r.cols - 2, 1, 1);
  } else {
    p->body = derwin(p->frame, r.rows, r.cols, 0, 0);
  }
  if(!p->body) {
    delwin(p->frame);
    free(p->title);
    free(p);
    return NULL;
  }
  wbkgd(p->body, COLOR_PAIR(SPZ_PAIR_TEXT));
  werase(p->body);
  return p;
}

void spz_panel_del(spz_panel_t *p)
{
  if(!p)
    return;
  if(p->body)
    delwin(p->body);
  if(p->frame)
    delwin(p->frame);
  free(p->title);
  free(p);
}

WINDOW *spz_panel_body(spz_panel_t *p)
{
  return p ? p->body : NULL;
}

void spz_panel_set_title(spz_panel_t *p, const char *title)
{
  if(!p)
    return;
  char *new_title = dup_title(title);
  /* If allocation fails, keep the old title rather than ending up titleless;
   * the caller can't easily recover from a NULL write here. */
  if(title && !new_title)
    return;
  free(p->title);
  p->title       = new_title;
  p->frame_dirty = true;
}

void spz_panel_refresh(spz_panel_t *p)
{
  if(!p)
    return;
  if(p->frame_dirty) {
    werase(p->frame);
    spz_border_draw(p->frame, p->rect.rows, p->rect.cols, p->title, p->border);
    p->frame_dirty = false;
  }
  wnoutrefresh(p->frame);
  /* touchwin so derwin's overlay is re-rendered even when ncurses thinks
   * the underlying frame already covers it (the body shares the frame's
   * cells via derwin). */
  touchwin(p->body);
  wnoutrefresh(p->body);
  doupdate();
}

void spz_panel_invalidate(spz_panel_t *p)
{
  if(!p)
    return;
  p->frame_dirty = true;
  touchwin(p->frame);
  touchwin(p->body);
}
