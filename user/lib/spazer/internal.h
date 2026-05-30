/**
 * @file user/lib/spazer/internal.h
 * @brief Translation-unit-local helpers shared between spazer modules.
 *
 * Not part of the public API. Anything declared here may change between
 * spazer releases without notice.
 */
#pragma once

#include <spazer/spazer.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Resolve a style preset to its ncurses colour-pair + attribute mask.
   *
   * Centralises the lookup so callers (label, status/header bar, menu) share
   * one table — adding a style means touching one switch.
   *
   * @param style  Style preset.
   * @param pair   Out: ncurses colour-pair index.
   * @param attr   Out: ncurses attribute mask (e.g. @c A_BOLD).
   */
  void spz_style_resolve(spz_style_t style, int *pair, chtype *attr);

  /**
   * @brief Draw a border + optional centred title in @p win.
   *
   * Assumes @p win is exactly @p rows x @p cols. @c ACS_* glyphs are
   * populated by ncurses at runtime — using @c waddch is the only portable
   * way in non-wide ncurses.
   *
   * @param win    Target window (a frame).
   * @param rows   Total rows of the frame.
   * @param cols   Total columns of the frame.
   * @param title  Centred title, NULL for none.
   * @param style  Border style; out-of-range values fall back to single.
   */
  void spz_border_draw(
      WINDOW *win, int rows, int cols, const char *title, spz_border_t style
  );

  /**
   * @brief Duplicate a NUL-terminated string into a freshly @c malloc'd buffer.
   *
   * @param s  Source string, NULL allowed (returns NULL).
   * @return   Heap copy, or NULL when @p s is NULL or allocation failed.
   */
  char *spz_strdup(const char *s);

  /**
   * @brief Clamp @p val to @c [0, max] inclusive.
   *
   * @param val  Value to clamp.
   * @param max  Upper bound; values below 0 collapse the range to @c {0}.
   * @return     Clamped value.
   */
  int   spz_clamp(int val, int max);

  /**
   * @brief Clamp @p r to the visible screen so out-of-bounds geometry never
   *        crashes ncurses with a NULL window.
   *
   * @param r  Rectangle to clamp.
   * @return   The clamped rectangle.
   */
  spz_rect_t spz_clamp_to_screen(spz_rect_t r);

#ifdef __cplusplus
} /* extern "C" */
#endif
