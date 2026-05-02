/**
 * @file user/shell/main.c
 * @brief vega REPL: read a line, hand it to vega_run().
 */

#include "shell.h"
#include "vega.h"
#include <stddef.h>
#include <stdlib.h>

/**
 * @brief Read a line of input from the user with basic line editing.
 * @param buf Buffer to store the line.
 * @param size Size of the buffer.
 * @return Number of characters read, or special value for EOF.
 */
static int read_line(char *buf, size_t size)
{
  size_t pos = 0;
  int    c;

  while(1) {
    c = sh_getchar();
    if(c < 0)
      continue;

    if(c == '\n' || c == '\r') {
      sh_putchar('\n');
      buf[pos] = '\0';
      return (int)pos;
    }

    if(c == '\b' || c == 127) {
      if(pos > 0) {
        pos--;
        sh_puts("\b \b");
      }
      continue;
    }

    if(c == 0x03) {
      sh_puts("^C\n");
      buf[0] = '\0';
      return 0;
    }

    if(c == 0x04) {
      if(pos == 0) {
        return -1;
      }
      continue;
    }

    if(c == 0x0C) {
      sh_clear();
      return 0;
    }

    /* ISO 8859-1 (same bytes as Unicode U+0080-U+00FF): allow printable high bytes after user sets `kbd fr`. */
    unsigned char uc = (unsigned char)c;
    if(((uc >= 32u && uc < 127u) || uc >= 160u) && pos < size - 1) {
      buf[pos++] = (char)c;
      sh_putchar((char)c);
    }
  }
}

/**
 * @brief Print the shell prompt with current working directory.
 */
static void print_prompt(void)
{
  char cwd[MAX_PATH];

  if(sh_getcwd(cwd, sizeof(cwd)) != NULL) {
    sh_puts("alcor2:");
    sh_puts(cwd);
    sh_puts("$ ");
  } else {
    sh_puts("alcor2> ");
  }
}

/* True if @p buf parses as a complete statement: no open quote, balanced
 * braces. Mirrors the lexer's quoting rules — single quotes are literal,
 * double quotes recognise \" \\ \$ as escapes. Brace counting is suppressed
 * inside either quote. A negative depth (`}` without `{`) is treated as
 * complete: let the parser surface the error rather than wedge the REPL. */
static int is_input_complete(const char *buf)
{
  int in_squote   = 0;
  int in_dquote   = 0;
  int brace_depth = 0;

  for(const char *p = buf; *p; p++) {
    char c = *p;
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
    if(c == '{')
      brace_depth++;
    else if(c == '}' && brace_depth > 0)
      brace_depth--;
  }
  return !in_squote && !in_dquote && brace_depth == 0;
}

/* Read input lines into @p buf until they form a complete statement. After
 * each newline we append '\n' so the lexer (which treats '\n' as SEMI) sees
 * a separator between stitched lines. PS2 ('> ') is shown for continuation
 * prompts. Returns total bytes accumulated, or -1 on EOF. */
static int read_complete_statement(char *buf, size_t size)
{
  size_t pos = 0;
  buf[0]     = '\0';

  while(1) {
    int len = read_line(buf + pos, size - pos);
    if(len < 0)
      return -1;

    pos += (size_t)len;
    if(pos >= size - 2) /* leave room for '\n' and '\0' */
      return (int)pos;

    buf[pos++] = '\n';
    buf[pos]   = '\0';

    if(is_input_complete(buf))
      return (int)pos;

    sh_puts("> ");
  }
}

/**
 * @brief Shell main entry point.
 *
 * Displays welcome message and enters the main read-eval-print loop.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code (0 on normal exit).
 */
int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  char line[MAX_CMD_LEN];

  sh_puts("\n");
  sh_puts("  vega " VEGA_VERSION " - Alcor2 shell\n");
  sh_puts("  Type 'help' for available commands.\n");
  sh_puts("\n");

  while(1) {
    print_prompt();

    int len = read_complete_statement(line, sizeof(line));

    if(len < 0) {
      sh_puts("exit\n");
      exit(0);
    }

    if(len > 0) {
      vega_run(line);
    }
  }

  return 0;
}
