/**
 * @file platform/parse.c
 * @brief Multi-line statement accumulator: brace/quote/heredoc-aware test for
 *        whether the buffer is a complete vega statement, and the loop that
 *        keeps reading continuation lines until it is.
 */

#include <boxdraw.h>
#include <shell/shell.h>
#include <stdio.h>
#include <theme.h>

#define MAX_HEREDOC_DELIM 64

/**
 * @brief True if @p buf parses as a complete statement.
 *
 * Mirrors the lexer's quoting rules — single quotes are literal, double
 * quotes recognise @c \\" @c \\\\ @c \\$ as escapes. Brace counting is
 * suppressed inside either quote.
 *
 * Heredoc tracking: when @c << (not @c <<<) appears outside quotes the next
 * word is captured as the delimiter; from the following newline onward the
 * walker watches each line for an exact match against the delimiter, and
 * stays incomplete until found. One heredoc per command for now. A negative
 * brace depth (@c } without @c {) is treated as complete: let the parser
 * surface the error rather than wedge the REPL.
 *
 * @param buf  NUL-terminated input buffer.
 * @return Non-zero if the input is complete, 0 if more input is needed.
 */
static int is_input_complete(const char *buf)
{
  int  in_squote   = 0;
  int  in_dquote   = 0;
  int  brace_depth = 0;
  int  want_delim  = 0;
  int  in_hd_body  = 0;
  char delim[MAX_HEREDOC_DELIM];
  int  dn = 0;
  char line_buf[MAX_HEREDOC_DELIM];
  int  ln = 0;

  for(const char *p = buf; *p; p++) {
    char c = *p;

    if(in_hd_body) {
      if(c == '\n') {
        line_buf[ln] = '\0';
        if(ln == dn) {
          int eq = 1;
          for(int i = 0; i < dn; i++)
            if(line_buf[i] != delim[i]) {
              eq = 0;
              break;
            }
          if(eq) {
            in_hd_body = 0;
            dn         = 0;
            ln         = 0;
            continue;
          }
        }
        ln = 0;
      } else if(ln < MAX_HEREDOC_DELIM - 1) {
        line_buf[ln++] = c;
      } else {
        ln = MAX_HEREDOC_DELIM - 1;
      }
      continue;
    }

    if(want_delim) {
      if(c == ' ' || c == '\t') {
        if(dn > 0)
          want_delim = 0;
        continue;
      }
      if(c == '\n') {
        if(dn > 0) {
          want_delim = 0;
          in_hd_body = 1;
          ln         = 0;
        }
        continue;
      }
      if(dn < MAX_HEREDOC_DELIM - 1)
        delim[dn++] = c;
      continue;
    }

    if(in_squote) {
      if(c == '\'')
        in_squote = 0;
      continue;
    }
    if(in_dquote) {
      if(c == '\\' && p[1] && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
        p++;
        continue;
      }
      if(c == '"')
        in_dquote = 0;
      continue;
    }
    if(c == '\'') {
      in_squote = 1;
      continue;
    }
    if(c == '"') {
      in_dquote = 1;
      continue;
    }
    if(c == '<' && p[1] == '<') {
      if(p[2] == '<') {
        p += 2;
        continue;
      }
      want_delim = 1;
      dn         = 0;
      p++;
      continue;
    }
    if(c == '\n') {
      if(dn > 0) {
        in_hd_body = 1;
        ln         = 0;
      }
      continue;
    }
    if(c == '{')
      brace_depth++;
    else if(c == '}' && brace_depth > 0)
      brace_depth--;
  }
  return !in_squote && !in_dquote && brace_depth == 0 && !want_delim &&
         !in_hd_body;
}

/**
 * @brief Drive ::sh_read_line in a loop until @p buf forms a complete
 *        statement (closed quotes, balanced braces, finished heredoc).
 *
 * The primary prompt (with decorative header) is used for the first line and
 * the @c │ » continuation prompt for subsequent lines. Each successful line
 * is appended to @p buf followed by @c \\n.
 *
 * @param buf   Destination accumulator (NUL-terminated on return).
 * @param size  Capacity of @p buf in bytes.
 * @return Total bytes accumulated (including separator newlines), @c RL_EOF
 *         if Ctrl-D was pressed on an empty primary prompt, or 0 if
 *         interrupted with Ctrl-C.
 */
int sh_read_complete_statement(char *buf, size_t size)
{
  size_t pos = 0;
  buf[0]     = '\0';

  /* Static bottom-line prompt — same every time: ╰─ $  */
  char prompt[128];
  sh_format_prompt(prompt, sizeof prompt);

  /* Continuation prompt for multi-line input: │ »  (indented) */
  char cont_prompt[64];
  (void)snprintf(
      cont_prompt, sizeof cont_prompt,
      THEME_ANSI_DIM BD_V " " THEME_ANSI_RESET /* │ space */
      THEME_ANSI_SUCCESS_B "\xc2\xbb" THEME_ANSI_RESET " "
  ); /* »  (U+00BB Latin-1) */

  const char *cur_prompt = prompt;

  while(1) {
    /* Top decorative line only for primary prompt, not continuation. */
    if(cur_prompt == prompt)
      sh_write_prompt_header();

    int len = sh_read_line(buf + pos, size - pos, cur_prompt);
    if(len == RL_EOF)
      return RL_EOF;
    if(len == RL_INTERRUPT)
      return 0;
    if(len == RL_CLEAR) {
      sh_clear();
      cur_prompt = prompt; /* reset to primary so header is shown */
      continue;
    }

    pos += (size_t)len;
    if(pos >= size - 2)
      return (int)pos;

    buf[pos++] = '\n';
    buf[pos]   = '\0';

    if(is_input_complete(buf))
      return (int)pos;

    cur_prompt = cont_prompt;
  }
}
