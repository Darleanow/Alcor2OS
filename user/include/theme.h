/**
 * @file theme.h
 * @brief Semantic colour roles — swap this file to change the active theme.
 *
 * All application code should include <theme.h>, never a palette directly.
 * Roles:
 *   PRIMARY  — main interactive element (directories, borders, selection)
 *   ACCENT   — secondary highlight (hostname, status bar, titles)
 *   SUCCESS  — positive / executable
 *   WARNING  — caution
 *   TEXT     — normal readable text
 *   SUBTEXT  — secondary / dimmer text (paths, hints)
 *   DIM      — muted chrome (separator lines, overlay)
 */

#pragma once

#include <catppuccin.h>

/* xterm-256 colour indices (for ncurses init_pair) */
#define THEME_COL_PRIMARY  CTPP_COL_BLUE
#define THEME_COL_ACCENT   CTPP_COL_MAUVE
#define THEME_COL_SUCCESS  CTPP_COL_GREEN
#define THEME_COL_WARNING  CTPP_COL_YELLOW
#define THEME_COL_TEXT     CTPP_COL_TEXT
#define THEME_COL_DIM      CTPP_COL_OVERLAY

/* True-colour ANSI escape sequences (for write()/printf() output) */
#define THEME_ANSI_RESET    CTPP_ANSI_RESET
#define THEME_ANSI_PRIMARY  CTPP_ANSI_BLUE
#define THEME_ANSI_PRIMARY_B CTPP_ANSI_BLUE_B
#define THEME_ANSI_ACCENT   CTPP_ANSI_MAUVE
#define THEME_ANSI_ACCENT_B CTPP_ANSI_MAUVE_B
#define THEME_ANSI_SUCCESS  CTPP_ANSI_GREEN
#define THEME_ANSI_SUCCESS_B CTPP_ANSI_GREEN_B
#define THEME_ANSI_WARNING  CTPP_ANSI_YELLOW
#define THEME_ANSI_TEXT     CTPP_ANSI_TEXT
#define THEME_ANSI_SUBTEXT  CTPP_ANSI_SUBTEXT1
#define THEME_ANSI_DIM      CTPP_ANSI_OVERLAY1
