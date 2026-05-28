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
 * @brief Persistent walker state for ::is_input_complete.
 *
 * Bundled so each sub-state handler can read/update one struct rather than
 * juggling individual ints and char arrays. Zero-init = "outside any state".
 */
typedef struct
{
  int  in_squote;   /**< Inside single-quoted string. */
  int  in_dquote;   /**< Inside double-quoted string. */
  int  brace_depth; /**< Net @c { minus @c } seen outside quotes. */
  int  want_delim;  /**< Just saw @c <<; accumulating the heredoc delimiter. */
  int  in_hd_body; /**< Inside the heredoc body, scanning for delimiter line. */
  char delim[MAX_HEREDOC_DELIM];    /**< Captured heredoc delimiter. */
  int  dn;                          /**< Bytes used in @c delim. */
  char line_buf[MAX_HEREDOC_DELIM]; /**< Current heredoc-body line so far. */
  int  ln;                          /**< Bytes used in @c line_buf. */
} parse_state_t;

/** @brief Compare @p st->line_buf (length @p st->ln) against @p st->delim
 *         (length @p st->dn). @return Non-zero if identical. */
static int delim_matches(const parse_state_t *st)
{
  if(st->ln != st->dn)
    return 0;
  for(int i = 0; i < st->dn; i++)
    if(st->line_buf[i] != st->delim[i])
      return 0;
  return 1;
}

/**
 * @brief Heredoc body: accumulate one line at a time, exit on delim match.
 *
 * @param st  State (mutated).
 * @param c   Current char.
 */
static void handle_hd_body(parse_state_t *st, char c)
{
  if(c != '\n') {
    if(st->ln < MAX_HEREDOC_DELIM - 1)
      st->line_buf[st->ln++] = c;
    return;
  }
  st->line_buf[st->ln] = '\0';
  if(delim_matches(st)) {
    st->in_hd_body = 0;
    st->dn         = 0;
  }
  st->ln = 0;
}

/**
 * @brief @c << delimiter-collection state: read the word that follows.
 *
 * Newline (with at least one delimiter char) transitions into the heredoc
 * body. Whitespace ends the delimiter; subsequent chars are normal input.
 *
 * @param st  State (mutated).
 * @param c   Current char.
 */
static void handle_want_delim(parse_state_t *st, char c)
{
  if(c == ' ' || c == '\t') {
    if(st->dn > 0)
      st->want_delim = 0;
    return;
  }
  if(c == '\n') {
    if(st->dn > 0) {
      st->want_delim = 0;
      st->in_hd_body = 1;
      st->ln         = 0;
    }
    return;
  }
  if(st->dn < MAX_HEREDOC_DELIM - 1)
    st->delim[st->dn++] = c;
}

/**
 * @brief Double-quoted state: handle @c \\" @c \\\\ @c \\$ escapes, exit on
 *        closing quote.
 *
 * @param st       State (mutated).
 * @param cursor   Pointer to the cursor into the source string; may be
 *                 advanced one position to consume an escape pair.
 */
static void handle_dquote(parse_state_t *st, const char **cursor)
{
  char c = **cursor;
  if(c == '\\') {
    char n = (*cursor)[1];
    if(n == '"' || n == '\\' || n == '$') {
      (*cursor)++;
      return;
    }
  }
  if(c == '"')
    st->in_dquote = 0;
}

/**
 * @brief Top-level dispatch: brace counting, quote/heredoc triggers.
 *
 * Only called when no specialised state is active.
 *
 * @param st       State (mutated).
 * @param cursor   Pointer to the cursor into the source string; may be
 *                 advanced to skip @c <<< or the second @c < of @c <<.
 */
static void handle_normal(parse_state_t *st, const char **cursor)
{
  char c = **cursor;
  switch(c) {
  case '\'':
    st->in_squote = 1;
    return;
  case '"':
    st->in_dquote = 1;
    return;
  case '<':
    if((*cursor)[1] != '<')
      return;
    if((*cursor)[2] == '<') {
      *cursor += 2;
      return;
    }
    st->want_delim = 1;
    st->dn         = 0;
    (*cursor)++;
    return;
  case '\n':
    if(st->dn > 0) {
      st->in_hd_body = 1;
      st->ln         = 0;
    }
    return;
  case '{':
    st->brace_depth++;
    return;
  case '}':
    if(st->brace_depth > 0)
      st->brace_depth--;
    return;
  default:
    return;
  }
}

/**
 * @brief True if @p buf parses as a complete vega statement.
 *
 * Mirrors the lexer's quoting rules — single quotes are literal, double
 * quotes recognise @c \\" @c \\\\ @c \\$ as escapes. Brace counting is
 * suppressed inside either quote. One heredoc per command. A negative brace
 * depth (@c } without @c {) is treated as complete: let the parser surface
 * the error rather than wedge the REPL.
 *
 * @param buf  NUL-terminated input buffer.
 * @return Non-zero if the input is complete, 0 if more input is needed.
 */
static int is_input_complete(const char *buf)
{
  parse_state_t st = {0};
  for(const char *p = buf; *p; p++) {
    if(st.in_hd_body) {
      handle_hd_body(&st, *p);
      continue;
    }
    if(st.want_delim) {
      handle_want_delim(&st, *p);
      continue;
    }
    if(st.in_squote) {
      if(*p == '\'')
        st.in_squote = 0;
      continue;
    }
    if(st.in_dquote) {
      handle_dquote(&st, &p);
      continue;
    }
    handle_normal(&st, &p);
  }
  return !st.in_squote && !st.in_dquote && st.brace_depth == 0 &&
         !st.want_delim && !st.in_hd_body;
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
