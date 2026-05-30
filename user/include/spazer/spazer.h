/**
 * @file spazer/spazer.h
 * @brief Public API for spazer — a thin, ncurses-based TUI toolkit.
 *
 * Layered on top of ncurses (panels, pads, windows are reified as opaque
 * handles). Caller owns every handle: every @c spz_*_new has a matching
 * @c spz_*_del, and refresh is explicit. No hidden globals beyond the
 * one-time @c spz_init / @c spz_shutdown pair that owns the colour palette.
 *
 * Threading: not thread-safe. Single-threaded TUI loop only.
 */
#pragma once

#include <curses.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include <spazer/palette.h>

#ifdef __cplusplus
extern "C"
{
#endif

  enum
  {
    SPZ_PAIR_BORDER = 1,
    SPZ_PAIR_TITLE  = 2,
    SPZ_PAIR_TEXT   = 3,
    SPZ_PAIR_ACCENT = 4,
    SPZ_PAIR_SELECT = 5,
    SPZ_PAIR_DIM    = 6,
    SPZ_PAIR_STATUS = 7,
  };

  typedef enum
  {
    SPZ_STYLE_NORMAL = 0,
    SPZ_STYLE_TITLE  = 1,
    SPZ_STYLE_ACCENT = 2,
    SPZ_STYLE_DIM    = 3,
    SPZ_STYLE_SELECT = 4,
    SPZ_STYLE_STATUS = 5,
  } spz_style_t;

  typedef enum
  {
    SPZ_BORDER_NONE   = 0,
    SPZ_BORDER_SINGLE = 1,
    SPZ_BORDER_DOUBLE = 2,
    SPZ_BORDER_HEAVY  = 3,
  } spz_border_t;

  typedef struct
  {
    int y, x, rows, cols;
  } spz_rect_t;

  /**
   * @brief Initialise the ncurses context and register the spazer palette.
   *
   * Must be called once before any other spz_* function. Sets raw mode,
   * disables echo, enables @c keypad on stdscr, hides the default cursor.
   * Idempotent: a second call returns success without re-initialising.
   *
   * @return 0 on success, -1 if ncurses initialisation failed.
   */
  int spz_init(void);

  /**
   * @brief Tear down the ncurses context.
   *
   * Releases palette state. After this every handle still alive is invalid
   * and any further @c spz_* call is undefined behaviour.
   */
  void                     spz_shutdown(void);

  typedef struct spz_panel spz_panel_t;

  /**
   * @brief Create a framed panel on the screen.
   *
   * @param r      On-screen rectangle, frame included. Clamped to stdscr.
   * @param title  Centred title text drawn in the top border, NULL for none.
   * @param b      Border style; @c SPZ_BORDER_NONE leaves the full rect
   *               available as the body.
   * @return       Heap-allocated handle, NULL on allocation failure. Caller
   *               owns; pair with @ref spz_panel_del.
   */
  spz_panel_t *spz_panel_new(spz_rect_t r, const char *title, spz_border_t b);

  /**
   * @brief Destroy a panel and free its windows.
   *
   * @param p  Panel to destroy. NULL is a no-op.
   */
  void spz_panel_del(spz_panel_t *p);

  /**
   * @brief Body window — caller paints here.
   *
   * Owned by the panel; do not @c delwin it. Valid until @ref spz_panel_del.
   *
   * @param p  Panel.
   * @return   Body @c WINDOW pointer, or NULL if @p p is NULL.
   */
  WINDOW *spz_panel_body(spz_panel_t *p);

  /**
   * @brief Replace the title and force a frame repaint on next refresh.
   *
   * @param p      Panel.
   * @param title  New title text, NULL to clear.
   */
  void spz_panel_set_title(spz_panel_t *p, const char *title);

  /**
   * @brief Push the panel's frame + body to the screen.
   *
   * Internally uses @c wnoutrefresh on both windows and a single @c doupdate.
   *
   * @param p  Panel.
   */
  void spz_panel_refresh(spz_panel_t *p);

  /**
   * @brief Mark the whole panel dirty so the next refresh repaints from
   *        scratch.
   *
   * Use after a resize, after toggling colour pairs at runtime, or after a
   * low-level escape sequence bypassed ncurses' virtual screen.
   *
   * @param p  Panel.
   */
  void                   spz_panel_invalidate(spz_panel_t *p);

  typedef struct spz_pad spz_pad_t;

  /**
   * @brief Create a panel whose body is a scrollable pad.
   *
   * The pad is an off-screen buffer of @p virt rows x cols; the panel body
   * acts as a viewport into it. Suitable for editors, log viewers, anything
   * larger than the on-screen body.
   *
   * @param outer   On-screen rectangle of the panel (frame + body).
   * @param virt    Virtual size of the underlying pad; @c virt.y / @c virt.x
   *                are ignored.
   * @param title   Border title, NULL for none.
   * @param b       Border style.
   * @return        Heap-allocated handle, NULL on failure.
   */
  spz_pad_t *spz_pad_new(
      spz_rect_t outer, spz_rect_t virt, const char *title, spz_border_t b
  );

  /**
   * @brief Destroy a pad and free its windows.
   *
   * @param p  Pad to destroy. NULL is a no-op.
   */
  void spz_pad_del(spz_pad_t *p);

  /**
   * @brief Pad @c WINDOW — caller paints with @c mvwaddstr /
   *        @c mvwaddwstr / etc.
   *
   * Owned by the pad; do not @c delwin it.
   *
   * @param p  Pad.
   * @return   Pad @c WINDOW pointer, or NULL if @p p is NULL.
   */
  WINDOW *spz_pad_buffer(spz_pad_t *p);

  /**
   * @brief Set the top-left viewport offset into the pad.
   *
   * Negative values clamp to 0; values past @c virt - viewport_size clamp
   * to the last fully-visible row/column.
   *
   * @param p    Pad.
   * @param row  Top-left row of the viewport, in pad coordinates.
   * @param col  Top-left column of the viewport, in pad coordinates.
   */
  void spz_pad_scroll_to(spz_pad_t *p, int row, int col);

  /**
   * @brief Set the cursor in pad-virtual coordinates and slide the viewport
   *        so the cursor stays visible.
   *
   * @param p    Pad.
   * @param row  Cursor row, in pad coordinates.
   * @param col  Cursor column, in pad coordinates.
   */
  void spz_pad_set_cursor(spz_pad_t *p, int row, int col);

  /**
   * @brief Push pad viewport + frame to the screen.
   *
   * @param p  Pad.
   */
  void spz_pad_refresh(spz_pad_t *p);

  /**
   * @brief Grow the underlying pad.
   *
   * Each axis grows independently: a request that shrinks one dimension is
   * silently clamped to the current size on that axis so callers can compute
   * a desired @c (rows, cols) without first reading the current geometry.
   * To shrink a pad, destroy it and create a new one.
   *
   * @param p          Pad.
   * @param virt_rows  Desired row count; clamped to >= current.
   * @param virt_cols  Desired column count; clamped to >= current.
   * @return           0 on success, -1 on allocation failure or invalid args.
   */
  int spz_pad_resize(spz_pad_t *p, int virt_rows, int virt_cols);

  typedef struct
  {
    const char *left;
    const char *center;
    const char *right;
    spz_style_t style;
  } spz_bar_t;

  /**
   * @brief Paint a one-row bar at the bottom of the screen.
   *
   * @param bar  Bar specification; NULL fields are skipped. Style defaults to
   *             @c SPZ_STYLE_STATUS if zero.
   */
  void spz_statusbar(const spz_bar_t *bar);

  /**
   * @brief Paint a one-row bar at the top of the screen.
   *
   * @param bar  Bar specification; NULL fields are skipped. Style defaults to
   *             @c SPZ_STYLE_TITLE if zero.
   */
  void spz_headerbar(const spz_bar_t *bar);

  typedef struct
  {
    spz_rect_t         rect;
    const char        *title;
    const char *const *items;
    int                n_items;
    int                initial;
  } spz_menu_t;

  /**
   * @brief Run a modal menu.
   *
   * Blocks until the user picks an item with Enter or aborts with ESC.
   * Restores the screen contents under the menu on return.
   *
   * @param m  Menu spec. @c initial is clamped to @c [0, n_items).
   * @return   Selected index, or -1 on ESC / error.
   */
  int spz_menu_run(const spz_menu_t *m);

  /**
   * @brief Draw a styled label at @p (y, x) in @p win.
   *
   * @param win    Target window.
   * @param y      Row in @p win.
   * @param x      Column in @p win.
   * @param text   UTF-8 text. NULL is a no-op.
   * @param style  Style preset; see @ref spz_style_t.
   */
  void
      spz_label(WINDOW *win, int y, int x, const char *text, spz_style_t style);

  /**
   * @brief Render a horizontal progress bar @p width cells wide.
   *
   * @param win    Target window.
   * @param y      Row in @p win.
   * @param x      Column in @p win.
   * @param width  Total cell width of the bar.
   * @param num    Filled units.
   * @param den    Total units; clamped to 1 to avoid division by zero.
   */
  void spz_progress(WINDOW *win, int y, int x, int width, int num, int den);

  /**
   * @brief Blank a region using the window's current background attribute.
   *
   * @param win   Target window.
   * @param y     Top row in @p win.
   * @param x     Left column in @p win.
   * @param rows  Row count.
   * @param cols  Column count.
   */
  void spz_clear_region(WINDOW *win, int y, int x, int rows, int cols);

  typedef enum
  {
    SPZ_EV_NONE   = 0,
    SPZ_EV_KEY    = 1,
    SPZ_EV_RESIZE = 2,
    SPZ_EV_MOUSE  = 3,
  } spz_event_kind_t;

  typedef enum
  {
    SPZ_KEY_NONE      = 0,
    SPZ_KEY_CHAR      = 1,
    SPZ_KEY_ENTER     = 2,
    SPZ_KEY_TAB       = 3,
    SPZ_KEY_BACKSPACE = 4,
    SPZ_KEY_ESCAPE    = 5,
    SPZ_KEY_UP        = 6,
    SPZ_KEY_DOWN      = 7,
    SPZ_KEY_LEFT      = 8,
    SPZ_KEY_RIGHT     = 9,
    SPZ_KEY_HOME      = 10,
    SPZ_KEY_END       = 11,
    SPZ_KEY_PAGE_UP   = 12,
    SPZ_KEY_PAGE_DOWN = 13,
    SPZ_KEY_DELETE    = 14,
    SPZ_KEY_F1        = 100,
    SPZ_KEY_F2        = 101,
    SPZ_KEY_F3        = 102,
    SPZ_KEY_F4        = 103,
    SPZ_KEY_F5        = 104,
    SPZ_KEY_F6        = 105,
    SPZ_KEY_F7        = 106,
    SPZ_KEY_F8        = 107,
    SPZ_KEY_F9        = 108,
    SPZ_KEY_F10       = 109,
    SPZ_KEY_F11       = 110,
    SPZ_KEY_F12       = 111,
  } spz_key_kind_t;

  typedef struct
  {
    spz_key_kind_t kind;
    wchar_t        ch;
    uint8_t        mods;
  } spz_key_t;

  typedef struct
  {
    int y, x;
    int button;
  } spz_mouse_t;

  typedef struct
  {
    spz_event_kind_t kind;
    union
    {
      spz_key_t   key;
      spz_mouse_t mouse;
    };
  } spz_event_t;

  /**
   * @brief Block on @p focus until one event is decoded.
   *
   * @p focus must be the window that owns @c keypad(TRUE) — pass the body of
   * the focused panel/pad so arrow keys decode to @c SPZ_KEY_UP/...
   *
   * @param focus  Focused window (drives @c wget_wch).
   * @param out    Filled with the decoded event on return.
   * @return       1 on event, 0 on timeout (currently never times out),
   *               -1 on error.
   */
  int spz_poll(WINDOW *focus, spz_event_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif
