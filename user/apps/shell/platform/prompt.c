/**
 * @file platform/prompt.c
 * @brief Shell prompt rendering: decorative header (cwd) and one-line input
 *        prompt with rounded box-drawing accents.
 */

#include <boxdraw.h>
#include <shell/shell.h>
#include <stdio.h>
#include <theme.h>

/* Semantic colour roles. */
#define PC_LINE THEME_ANSI_DIM
#define PC_HOST THEME_ANSI_ACCENT_B
#define PC_PATH THEME_ANSI_SUBTEXT
#define PC_DOLS THEME_ANSI_SUCCESS_B
#define PC_RS   THEME_ANSI_RESET

/* Box-drawing; rounded corners are arc-rasterised in atlas.c. */
#define PC_TL BD_TL_R /* ╭ */
#define PC_BL BD_BL_R /* ╰ */
#define PC_H  BD_H    /* ─ */

/**
 * @brief Paint the decorative header line: `╭─ alcor2 ─ <cwd>`.
 *
 * Written via ::sh_puts; fits on one row and ends with @c \\n.
 */
void sh_write_prompt_header(void)
{
  char        cwd[MAX_PATH];
  const char *path = sh_getcwd(cwd, sizeof cwd) ? cwd : "/";
  sh_puts(PC_LINE PC_TL PC_H " " PC_RS); /* ╭─ space */
  sh_puts(PC_HOST "alcor2" PC_RS);
  sh_puts(PC_LINE " " PC_H " " PC_RS); /* space ─ space */
  sh_puts(PC_PATH);
  sh_puts(path);
  sh_puts(PC_RS "\n");
}

/**
 * @brief Format the bottom-line prompt `╰─ $ ` into @p out.
 *
 * Includes ANSI colour escapes; visible column width is 4.
 *
 * @param out  Destination buffer.
 * @param cap  Capacity of @p out (must be ≥ ~32 bytes for the full prompt).
 */
void sh_format_prompt(char *out, size_t cap)
{
  (void)snprintf(
      out, cap,
      PC_LINE PC_BL PC_H " " PC_RS /* ╰─ space */
      PC_DOLS "$" PC_RS " "
  ); /* $ space  */
}
