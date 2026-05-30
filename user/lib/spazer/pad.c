/**
 * @file user/lib/spazer/pad.c
 * @brief Scrollable pad inside a framed panel.
 *
 * The pad is an off-screen @c newpad of @c virt rows/cols; the panel body
 * acts as a viewport into it. The caller paints anywhere in the pad with the
 * standard ncurses API and we project a slice of it onto the screen with
 * @c pnoutrefresh on every refresh. No DECSTBM / IL / DL acrobatics needed at
 * the terminal layer — repainting the visible slice is cheap and correct.
 */

#include "internal.h"

#include <stdlib.h>

/**
 * @brief Concrete layout of a pad handle.
 */
struct spz_pad
{
  WINDOW      *frame;       /**< Outer window, owns border + title. */
  WINDOW      *body;        /**< Off-screen @c newpad of @c virt size. */
  spz_rect_t   outer;       /**< On-screen rect of @c frame. */
  spz_rect_t   inner;       /**< On-screen rect of the viewport. */
  int          virt_rows;   /**< Virtual rows of @c body. */
  int          virt_cols;   /**< Virtual columns of @c body. */
  int          off_row;     /**< Top of the viewport, in pad coords. */
  int          off_col;     /**< Left of the viewport, in pad coords. */
  int          cur_row;     /**< Cursor row, in pad coords. */
  int          cur_col;     /**< Cursor column, in pad coords. */
  spz_border_t border;      /**< Border style. */
  char        *title;       /**< Heap-owned copy, NULL for none. */
  bool         frame_dirty; /**< Border needs to be re-drawn on next refresh. */
};

/** @brief Upper bound for @c off_row so the viewport stays inside the pad. */
static int max_off_row(const spz_pad_t *p)
{
  int m = p->virt_rows - p->inner.rows;
  return m < 0 ? 0 : m;
}

/** @brief Upper bound for @c off_col. Symmetric of @ref max_off_row. */
static int max_off_col(const spz_pad_t *p)
{
  int m = p->virt_cols - p->inner.cols;
  return m < 0 ? 0 : m;
}

/**
 * @brief Slide the viewport so the cursor is visible, then clamp to bounds.
 *
 * @param p  Pad.
 */
static void follow_cursor(spz_pad_t *p)
{
  const int view_h = p->inner.rows;
  const int view_w = p->inner.cols;

  if(p->cur_row < p->off_row)
    p->off_row = p->cur_row;
  else if(p->cur_row >= p->off_row + view_h)
    p->off_row = p->cur_row - view_h + 1;

  if(p->cur_col < p->off_col)
    p->off_col = p->cur_col;
  else if(p->cur_col >= p->off_col + view_w)
    p->off_col = p->cur_col - view_w + 1;

  p->off_row = spz_clamp(p->off_row, max_off_row(p));
  p->off_col = spz_clamp(p->off_col, max_off_col(p));
}

spz_pad_t *spz_pad_new(
    spz_rect_t outer, spz_rect_t virt, const char *title, spz_border_t b
)
{
  if(virt.rows < 1 || virt.cols < 1)
    return NULL;
  if(outer.rows < 1 || outer.cols < 1)
    return NULL;

  bool has_border = (b != SPZ_BORDER_NONE);
  if(has_border && (outer.rows < 3 || outer.cols < 3))
    return NULL;

  spz_pad_t *p = (spz_pad_t *)calloc(1, sizeof(*p));
  if(!p)
    return NULL;

  p->outer       = outer;
  p->border      = b;
  p->title       = spz_strdup(title);
  p->frame_dirty = true;

  p->inner.y     = has_border ? outer.y + 1 : outer.y;
  p->inner.x     = has_border ? outer.x + 1 : outer.x;
  p->inner.rows  = has_border ? outer.rows - 2 : outer.rows;
  p->inner.cols  = has_border ? outer.cols - 2 : outer.cols;

  p->frame       = newwin(outer.rows, outer.cols, outer.y, outer.x);
  if(!p->frame) {
    free(p->title);
    free(p);
    return NULL;
  }
  wbkgd(p->frame, COLOR_PAIR(SPZ_PAIR_TEXT));

  /* Pad must be >= viewport, else pnoutrefresh clips silently. */
  int pad_rows = (virt.rows > p->inner.rows) ? virt.rows : p->inner.rows;
  int pad_cols = (virt.cols > p->inner.cols) ? virt.cols : p->inner.cols;
  p->body      = newpad(pad_rows, pad_cols);
  if(!p->body) {
    delwin(p->frame);
    free(p->title);
    free(p);
    return NULL;
  }
  p->virt_rows = pad_rows;
  p->virt_cols = pad_cols;
  wbkgd(p->body, COLOR_PAIR(SPZ_PAIR_TEXT));
  werase(p->body);
  return p;
}

void spz_pad_del(spz_pad_t *p)
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

WINDOW *spz_pad_buffer(spz_pad_t *p)
{
  return p ? p->body : NULL;
}

void spz_pad_scroll_to(spz_pad_t *p, int row, int col)
{
  if(!p)
    return;
  p->off_row = spz_clamp(row, max_off_row(p));
  p->off_col = spz_clamp(col, max_off_col(p));
}

void spz_pad_set_cursor(spz_pad_t *p, int row, int col)
{
  if(!p)
    return;
  p->cur_row = spz_clamp(row, p->virt_rows - 1);
  p->cur_col = spz_clamp(col, p->virt_cols - 1);
  follow_cursor(p);
}

void spz_pad_refresh(spz_pad_t *p)
{
  if(!p)
    return;

  if(p->frame_dirty) {
    werase(p->frame);
    spz_border_draw(p->frame, p->outer.rows, p->outer.cols, p->title, p->border);
    p->frame_dirty = false;
  }
  /* Transient newwins (status bar) can run doupdate against a stale curscr,
   * so re-emit the border every frame. */
  touchwin(p->frame);
  wnoutrefresh(p->frame);

  /* pnoutrefresh propagates the pad cursor as the hardware cursor. */
  wmove(p->body, p->cur_row, p->cur_col);
  /* werase + paint inside a pad can be dropped when the viewport offset has
   * not moved; redrawwin marks every line dirty so the projection is
   * complete. */
  redrawwin(p->body);

  int dst_y1 = p->inner.y + p->inner.rows - 1;
  int dst_x1 = p->inner.x + p->inner.cols - 1;
  pnoutrefresh(
      p->body, p->off_row, p->off_col, p->inner.y, p->inner.x, dst_y1, dst_x1
  );

  doupdate();
}

int spz_pad_resize(spz_pad_t *p, int virt_rows, int virt_cols)
{
  if(!p || virt_rows < 1 || virt_cols < 1)
    return -1;

  /* Each axis grows independently — a request that would shrink one axis is
   * clamped to the current size on that axis. */
  int new_rows = (virt_rows > p->virt_rows) ? virt_rows : p->virt_rows;
  int new_cols = (virt_cols > p->virt_cols) ? virt_cols : p->virt_cols;
  if(new_rows == p->virt_rows && new_cols == p->virt_cols)
    return 0;

  if(wresize(p->body, new_rows, new_cols) != OK)
    return -1;
  p->virt_rows = new_rows;
  p->virt_cols = new_cols;
  return 0;
}
