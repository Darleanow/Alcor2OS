#pragma once

/* Catppuccin Mocha — xterm-256 indices (SPZ_COL_*) and true-colour ANSI
 * escapes (SPZ_ANSI_*).  No <curses.h> dependency; include this directly
 * from write()-based tools (ls, shell prompt) or via spazer.h for TUIs.
 *
 *   colour    hex       xterm-256
 *   Mauve     #CBA6F7   183
 *   Blue      #89B4FA   111
 *   Green     #A6E3A1   151
 *   Yellow    #F9E2AF   223
 *   Text      #CDD6F4   252
 *   Subtext1  #BAC2DE   146
 *   Overlay1  #6E738D   243
 */

#define SPZ_COL_MAUVE    183
#define SPZ_COL_BLUE     111
#define SPZ_COL_GREEN    151
#define SPZ_COL_YELLOW   223
#define SPZ_COL_TEXT     252
#define SPZ_COL_OVERLAY  243

#define SPZ_ANSI_RESET    "\033[0m"
#define SPZ_ANSI_MAUVE    "\033[38;2;203;166;247m"
#define SPZ_ANSI_MAUVE_B  "\033[1;38;2;203;166;247m"
#define SPZ_ANSI_BLUE     "\033[38;2;137;180;250m"
#define SPZ_ANSI_BLUE_B   "\033[1;38;2;137;180;250m"
#define SPZ_ANSI_GREEN    "\033[38;2;166;227;161m"
#define SPZ_ANSI_GREEN_B  "\033[1;38;2;166;227;161m"
#define SPZ_ANSI_YELLOW   "\033[38;2;249;226;175m"
#define SPZ_ANSI_TEXT     "\033[38;2;205;214;244m"
#define SPZ_ANSI_SUBTEXT1 "\033[38;2;186;194;222m"
#define SPZ_ANSI_OVERLAY1 "\033[38;2;110;115;141m"

/* UTF-8 box-drawing — use in write()/printf() output only.
 * Inside ncurses windows, use ACS_* macros (waddch) instead. */
#define SPZ_H       "\xe2\x94\x80"  /* ─ U+2500 */
#define SPZ_V       "\xe2\x94\x82"  /* │ U+2502 */
#define SPZ_TL      "\xe2\x94\x8c"  /* ┌ U+250C */
#define SPZ_TR      "\xe2\x94\x90"  /* ┐ U+2510 */
#define SPZ_BL      "\xe2\x94\x94"  /* └ U+2514 */
#define SPZ_BR      "\xe2\x94\x98"  /* ┘ U+2518 */
#define SPZ_TL_R    "\xe2\x95\xad"  /* ╭ U+256D */
#define SPZ_TR_R    "\xe2\x95\xae"  /* ╮ U+256E */
#define SPZ_BR_R    "\xe2\x95\xaf"  /* ╯ U+256F */
#define SPZ_BL_R    "\xe2\x95\xb0"  /* ╰ U+2570 */
#define SPZ_LT      "\xe2\x94\x9c"  /* ├ U+251C */
#define SPZ_RT      "\xe2\x94\xa4"  /* ┤ U+2524 */
#define SPZ_ARROW_R "\xe2\x86\x92"  /* → U+2192 */
