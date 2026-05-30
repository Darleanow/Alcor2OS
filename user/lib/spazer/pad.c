/**
 * @file user/lib/spazer/pad.c
 * @brief Scrollable pad inside a framed panel.
 *
 * The pad is an off-screen @c newpad of @c virt rows/cols; the panel body
 * acts as a viewport. The caller paints anywhere in the pad with the
 * standard ncurses API and we project a window of it onto the screen with
 * @c pnoutrefresh. No diff games, no DECSTBM acrobatics in the kernel —
 * ncurses just repaints the visible slice from the pad on every refresh.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Concrete layout of a pad handle.
 *
 * @c frame  outer window holding the border + title.
 * @c body   off-screen pad of @c virt size; caller paints here.
 * @c outer  on-screen rect of @c frame.
 * @c inner  on-screen rect of the viewport (frame minus border).
 * @c virt   current virtual rows/cols of @c body.
 * @c off    top-left of the viewport in pad coordinates.
 * @c cur    current cursor position in pad coordinates.
 */
struct spz_pad
{
  WINDOW      *frame;
  WINDOW      *body;
  spz_rect_t   outer;
  spz_rect_t   inner;
  int          virt_rows, virt_cols;
  int          off_row, off_col;
  int          cur_row, cur_col;
  spz_border_t border;
  char        *title;
  bool         frame_dirty;
};

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

/**
 * @brief Clamp @p val to @c [0, max].
 */
static int clamp(int val, int max)
{
  if(val < 0)
    return 0;
  if(val > max)
    return max;
  return val;
}

/**
 * @brief Recompute @c off so @c (cur_row, cur_col) is inside the viewport.
 *
 * If the cursor is above the viewport, slide up; below, slide down; same
 * logic horizontally. Side-effects @p p->off_row / @p p->off_col.
 *
 * @param p  Pad.
 */
static void follow_cursor(spz_pad_t *p)
{
  int view_h = p->inner.rows;
  int view_w = p->inner.cols;
  if(p->cur_row < p->off_row)
    p->off_row = p->cur_row;
  else if(p->cur_row >= p->off_row + view_h)
    p->off_row = p->cur_row - view_h + 1;
  if(p->cur_col < p->off_col)
    p->off_col = p->cur_col;
  else if(p->cur_col >= p->off_col + view_w)
    p->off_col = p->cur_col - view_w + 1;

  int max_off_row = p->virt_rows - view_h;
  int max_off_col = p->virt_cols - view_w;
  if(max_off_row < 0)
    max_off_row = 0;
  if(max_off_col < 0)
    max_off_col = 0;
  p->off_row = clamp(p->off_row, max_off_row);
  p->off_col = clamp(p->off_col, max_off_col);
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
  p->virt_rows   = virt.rows;
  p->virt_cols   = virt.cols;
  p->title       = dup_title(title);
  p->frame_dirty = true;

  p->inner.y    = has_border ? outer.y + 1 : outer.y;
  p->inner.x    = has_border ? outer.x + 1 : outer.x;
  p->inner.rows = has_border ? outer.rows - 2 : outer.rows;
  p->inner.cols = has_border ? outer.cols - 2 : outer.cols;

  p->frame = newwin(outer.rows, outer.cols, outer.y, outer.x);
  if(!p->frame) {
    free(p->title);
    free(p);
    return NULL;
  }
  wbkgd(p->frame, COLOR_PAIR(SPZ_PAIR_TEXT));

  /* Pad must be at least as large as the viewport so pnoutrefresh has a full
   * source region to project — otherwise ncurses clips silently. */
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
  int max_off_row = p->virt_rows - p->inner.rows;
  int max_off_col = p->virt_cols - p->inner.cols;
  if(max_off_row < 0)
    max_off_row = 0;
  if(max_off_col < 0)
    max_off_col = 0;
  p->off_row = clamp(row, max_off_row);
  p->off_col = clamp(col, max_off_col);
}

void spz_pad_set_cursor(spz_pad_t *p, int row, int col)
{
  if(!p)
    return;
  p->cur_row = clamp(row, p->virt_rows - 1);
  p->cur_col = clamp(col, p->virt_cols - 1);
  follow_cursor(p);
}

void spz_pad_refresh(spz_pad_t *p)
{
  if(!p)
    return;
  if(p->frame_dirty) {
    werase(p->frame);
    spz_border_draw(
        p->frame, p->outer.rows, p->outer.cols, p->title, p->border
    );
    p->frame_dirty = false;
  }
  /* touchwin so wnoutrefresh always re-emits the frame / body even when
   * ncurses believes curscr is already up to date — a transient newwin
   * (status bar) can run a doupdate that pushes a stale curscr to screen
   * and lose pixels. */
  touchwin(p->frame);
  wnoutrefresh(p->frame);

  /* Move the pad's logical cursor before pnoutrefresh so the projected
   * cursor lands at the right screen cell — pnoutrefresh propagates the
   * pad cursor for us. */
  wmove(p->body, p->cur_row, p->cur_col);
  /* redrawwin marks every line of the pad as dirty, forcing pnoutrefresh
   * to re-project the full source rectangle. touchwin alone leaves
   * already-clean lines of the pad untouched, which is why a werase + paint
   * cycle inside the pad can be silently dropped when the viewport offset
   * has not moved. */
  redrawwin(p->body);

  /* Project the [off_row, off_col] window of the pad onto the inner rect.
   * The src right/bottom is inclusive in pnoutrefresh. */
  int src_y0 = p->off_row;
  int src_x0 = p->off_col;
  int dst_y0 = p->inner.y;
  int dst_x0 = p->inner.x;
  int dst_y1 = dst_y0 + p->inner.rows - 1;
  int dst_x1 = dst_x0 + p->inner.cols - 1;
  pnoutrefresh(p->body, src_y0, src_x0, dst_y0, dst_x0, dst_y1, dst_x1);

  doupdate();
}

int spz_pad_resize(spz_pad_t *p, int virt_rows, int virt_cols)
{
  if(!p || virt_rows < p->virt_rows || virt_cols < p->virt_cols)
    return -1;
  if(virt_rows == p->virt_rows && virt_cols == p->virt_cols)
    return 0;

  /* wresize on a pad keeps existing cells in place. */
  if(wresize(p->body, virt_rows, virt_cols) != OK)
    return -1;
  p->virt_rows = virt_rows;
  p->virt_cols = virt_cols;
  return 0;
}
