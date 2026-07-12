/**
 * @file user/lib/spazer/panel.c
 * @brief Framed panel: outer frame window + derived body subwindow.
 *
 * One ncurses @c WINDOW (frame) plus a @c derwin (body) sharing its cells.
 * The caller paints into @c body; we own the title string and the
 * frame-dirty flag that drives lazy border repainting.
 */

#include "internal.h"

#include <stdlib.h>

/**
 * @brief Concrete layout of a panel handle.
 */
struct spz_panel
{
  WINDOW      *frame;       /**< Outer window, owns border + title. */
  WINDOW      *body;        /**< Subwindow of @c frame for caller painting. */
  spz_rect_t   rect;        /**< On-screen rectangle of @c frame. */
  spz_border_t border;      /**< Border style; @c NONE means full-rect body. */
  char        *title;       /**< Heap-owned copy, NULL for none. */
  bool         frame_dirty; /**< Border needs to be re-drawn on next refresh. */
};

spz_panel_t *spz_panel_new(spz_rect_t r, const char *title, spz_border_t b)
{
  r = spz_clamp_to_screen(r);
  if(r.rows < 1 || r.cols < 1)
    return NULL;

  bool has_border = (b != SPZ_BORDER_NONE);
  /* Bordered panels need 3x3 minimum so the body has at least one cell. */
  if(has_border && (r.rows < 3 || r.cols < 3))
    return NULL;

  spz_panel_t *p = (spz_panel_t *)calloc(1, sizeof(*p));
  if(!p)
    return NULL;

  p->rect        = r;
  p->border      = b;
  p->title       = spz_strdup(title);
  p->frame_dirty = true;

  p->frame = newwin(r.rows, r.cols, r.y, r.x);
  if(!p->frame) {
    free(p->title);
    free(p);
    return NULL;
  }
  wbkgd(p->frame, COLOR_PAIR(SPZ_PAIR_TEXT));

  if(has_border)
    p->body = derwin(p->frame, r.rows - 2, r.cols - 2, 1, 1);
  else
    p->body = derwin(p->frame, r.rows, r.cols, 0, 0);

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
  char *new_title = spz_strdup(title);
  /* On allocation failure, keep the old title rather than dropping it. */
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
  /* derwin shares cells with the frame; touch so the body overlay re-renders.
   */
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
