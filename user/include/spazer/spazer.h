#pragma once

#include <curses.h>
#include <spazer/palette.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ncurses colour-pair IDs registered by spz_init().
 *
 *  Pair    Foreground         Background
 *  BORDER  Blue               terminal default
 *  TITLE   Mauve              terminal default  (A_BOLD)
 *  TEXT    Text               terminal default
 *  ACCENT  Green              terminal default  (A_BOLD)
 *  SELECT  terminal default   Blue
 *  DIM     Overlay            terminal default
 *  STATUS  terminal default   Mauve             (A_BOLD)
 */
#define SPZ_PAIR_BORDER   1
#define SPZ_PAIR_TITLE    2
#define SPZ_PAIR_TEXT     3
#define SPZ_PAIR_ACCENT   4
#define SPZ_PAIR_SELECT   5
#define SPZ_PAIR_DIM      6
#define SPZ_PAIR_STATUS   7

#define SPZ_STYLE_NORMAL  0
#define SPZ_STYLE_TITLE   1
#define SPZ_STYLE_ACCENT  2
#define SPZ_STYLE_DIM     3
#define SPZ_STYLE_SELECT  4

#define SPZ_BORDER_SINGLE 0 /* ┌─┐│└┘ */
#define SPZ_BORDER_DOUBLE 1 /* ╔═╗║╚╝ */
#define SPZ_BORDER_HEAVY  2 /* ┏━┓┃┗┛ */

  typedef struct spz_panel
  {
    WINDOW *frame;
    WINDOW *body;
    int     y, x, rows, cols;
    int     border_style;
  } spz_panel_t;

  void         spz_init(void);

  spz_panel_t *spz_panel_new(
      int y, int x, int rows, int cols, const char *title, int border_style
  );

  void spz_panel_refresh(spz_panel_t *p);
  void spz_panel_redraw(spz_panel_t *p, const char *title);
  void spz_panel_del(spz_panel_t *p);

  void spz_progress(WINDOW *win, int y, int x, int width, int num, int den);

  void spz_statusbar(const char *left, const char *center, const char *right);

  int  spz_menu(
       int y, int x, const char *const *items, int n, const char *title
   );

  void spz_label(WINDOW *win, int y, int x, const char *text, int style);

  void spz_clear_region(WINDOW *win, int y, int x, int rows, int cols);

#ifdef __cplusplus
} /* extern "C" */
#endif
