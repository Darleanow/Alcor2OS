/**
 * @file user/lib/spazer/input.c
 * @brief Decode one ncurses key event into a high-level @ref spz_event_t.
 *
 * The translation table is the single source of truth so callers stop
 * sprinkling case @c KEY_LEFT/@c '\n'/etc. across their input loops.
 */

#include "internal.h"

#include <wchar.h>

/**
 * @brief Convert a raw @c wget_wch result into an event.
 *
 * @param kind  @c wget_wch return code (@c OK or @c KEY_CODE_YES).
 * @param ch    Decoded codepoint (printable / control) or @c KEY_* code.
 * @param out   Out: filled event.
 * @return      1 on event filled, 0 if nothing actionable.
 */
static int decode(int kind, wint_t ch, spz_event_t *out)
{
  out->kind     = SPZ_EV_KEY;
  out->key.mods = 0;
  out->key.ch   = 0;

  if(kind == KEY_CODE_YES) {
    switch(ch) {
    case KEY_RESIZE:
      out->kind = SPZ_EV_RESIZE;
      return 1;
    case KEY_UP:
      out->key.kind = SPZ_KEY_UP;
      return 1;
    case KEY_DOWN:
      out->key.kind = SPZ_KEY_DOWN;
      return 1;
    case KEY_LEFT:
      out->key.kind = SPZ_KEY_LEFT;
      return 1;
    case KEY_RIGHT:
      out->key.kind = SPZ_KEY_RIGHT;
      return 1;
    case KEY_HOME:
      out->key.kind = SPZ_KEY_HOME;
      return 1;
    case KEY_END:
      out->key.kind = SPZ_KEY_END;
      return 1;
    case KEY_PPAGE:
      out->key.kind = SPZ_KEY_PAGE_UP;
      return 1;
    case KEY_NPAGE:
      out->key.kind = SPZ_KEY_PAGE_DOWN;
      return 1;
    case KEY_BACKSPACE:
      out->key.kind = SPZ_KEY_BACKSPACE;
      return 1;
    case KEY_DC:
      out->key.kind = SPZ_KEY_DELETE;
      return 1;
    case KEY_ENTER:
      out->key.kind = SPZ_KEY_ENTER;
      return 1;
    default:
      /* F-keys map linearly: KEY_F(1) = SPZ_KEY_F1, KEY_F(2) = +1, etc. */
      if(ch >= KEY_F(1) && ch <= KEY_F(12)) {
        out->key.kind = (spz_key_kind_t)(SPZ_KEY_F1 + (ch - KEY_F(1)));
        return 1;
      }
      out->kind = SPZ_EV_NONE;
      return 0;
    }
  }

  /* OK path: ch is a wide character. Map a few control codes that
   * terminals send as bytes rather than KEY_* codes. */
  switch(ch) {
  case L'\r':
  case L'\n':
    out->key.kind = SPZ_KEY_ENTER;
    return 1;
  case L'\t':
    out->key.kind = SPZ_KEY_TAB;
    return 1;
  case 27:
    out->key.kind = SPZ_KEY_ESCAPE;
    return 1;
  case 127:
  case 8:
    /* DEL (0x7F) and BS (0x08) — both used as "backspace" by various
     * terminals; KEY_BACKSPACE catches the keypad-decoded form above. */
    out->key.kind = SPZ_KEY_BACKSPACE;
    return 1;
  default:
    out->key.kind = SPZ_KEY_CHAR;
    out->key.ch   = (wchar_t)ch;
    return 1;
  }
}

int spz_poll(WINDOW *focus, spz_event_t *out)
{
  if(!focus || !out)
    return -1;
  wint_t ch = 0;
  int    rc = wget_wch(focus, &ch);
  if(rc == ERR)
    return -1;
  if(!decode(rc, ch, out)) {
    out->kind     = SPZ_EV_NONE;
    out->key.kind = SPZ_KEY_NONE;
  }
  return 1;
}
