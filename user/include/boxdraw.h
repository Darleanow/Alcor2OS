/**
 * @file boxdraw.h
 * @brief UTF-8 box-drawing character literals for write()/printf() output.
 *
 * Inside ncurses windows use ACS_* macros with waddch() instead — waddstr()
 * with these strings outputs each byte as a separate cell in non-wide ncurses.
 */

#pragma once

#define BD_H       "\xe2\x94\x80"  /* ─ U+2500 */
#define BD_V       "\xe2\x94\x82"  /* │ U+2502 */
#define BD_TL      "\xe2\x94\x8c"  /* ┌ U+250C */
#define BD_TR      "\xe2\x94\x90"  /* ┐ U+2510 */
#define BD_BL      "\xe2\x94\x94"  /* └ U+2514 */
#define BD_BR      "\xe2\x94\x98"  /* ┘ U+2518 */
#define BD_TL_R    "\xe2\x95\xad"  /* ╭ U+256D */
#define BD_TR_R    "\xe2\x95\xae"  /* ╮ U+256E */
#define BD_BR_R    "\xe2\x95\xaf"  /* ╯ U+256F */
#define BD_BL_R    "\xe2\x95\xb0"  /* ╰ U+2570 */
#define BD_LT      "\xe2\x94\x9c"  /* ├ U+251C */
#define BD_RT      "\xe2\x94\xa4"  /* ┤ U+2524 */
#define BD_ARROW_R "\xe2\x86\x92"  /* → U+2192 */
