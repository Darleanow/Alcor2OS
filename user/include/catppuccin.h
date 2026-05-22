/**
 * @file catppuccin.h
 * @brief Catppuccin Mocha palette — xterm-256 indices and true-colour ANSI escapes.
 */

#pragma once

/*
 *   colour    hex       xterm-256
 *   Mauve     #CBA6F7   183
 *   Blue      #89B4FA   111
 *   Green     #A6E3A1   151
 *   Yellow    #F9E2AF   223
 *   Text      #CDD6F4   252
 *   Subtext1  #BAC2DE   146
 *   Overlay1  #6E738D   243
 */

/* xterm-256 colour indices (for ncurses init_pair) */
#define CTPP_COL_MAUVE    183
#define CTPP_COL_BLUE     111
#define CTPP_COL_GREEN    151
#define CTPP_COL_YELLOW   223
#define CTPP_COL_TEXT     252
#define CTPP_COL_OVERLAY  243

/* True-colour ANSI escape sequences (for write()/printf() output) */
#define CTPP_ANSI_RESET    "\033[0m"
#define CTPP_ANSI_MAUVE    "\033[38;2;203;166;247m"
#define CTPP_ANSI_MAUVE_B  "\033[1;38;2;203;166;247m"
#define CTPP_ANSI_BLUE     "\033[38;2;137;180;250m"
#define CTPP_ANSI_BLUE_B   "\033[1;38;2;137;180;250m"
#define CTPP_ANSI_GREEN    "\033[38;2;166;227;161m"
#define CTPP_ANSI_GREEN_B  "\033[1;38;2;166;227;161m"
#define CTPP_ANSI_YELLOW   "\033[38;2;249;226;175m"
#define CTPP_ANSI_TEXT     "\033[38;2;205;214;244m"
#define CTPP_ANSI_SUBTEXT1 "\033[38;2;186;194;222m"
#define CTPP_ANSI_OVERLAY1 "\033[38;2;110;115;141m"
