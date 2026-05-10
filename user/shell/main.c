/**
 * @file user/shell/main.c
 * @brief vega REPL: read a line, hand it to vega_run().
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vega/fb_tty.h>
#include <vega/shell.h>
#include <vega/vega.h>

#ifndef VEGA_VERSION
#define VEGA_VERSION "1.0.0"
#endif

static void fb_cursor_after_edit(void)
{
  if(sh_fb_tty_active())
    sh_fb_tty_cursor_after_edit();
}

/* Per-call line-editor state for read_line(): ESC pushback + UTF-8 accumulator.
 */
typedef struct
{
  int      pushback;
  unsigned utf8_rem;
  uint32_t utf8_partial;
} LineEditState;

static LineEditState s_edit = {.pushback = -1};

static void          line_edit_reset(LineEditState *s)
{
  s->pushback     = -1;
  s->utf8_rem     = 0;
  s->utf8_partial = 0;
}

/* Encode @p cp as UTF-8, append to @p buf, and echo each byte to the terminal.
 * Returns 0 on success, -1 if the buffer would overflow or cp is out of range.
 */
static int store_utf8_cp(char *buf, size_t size, size_t *pos, uint32_t cp)
{
  unsigned char enc[3];
  int           n;

  if(cp < 0x80u) {
    enc[0] = (unsigned char)cp;
    n      = 1;
  } else if(cp < 0x800u) {
    enc[0] = (unsigned char)(0xc0u | ((cp >> 6) & 0x1fu));
    enc[1] = (unsigned char)(0x80u | (cp & 0x3fu));
    n      = 2;
  } else if(cp < 0x10000u) {
    enc[0] = (unsigned char)(0xe0u | ((cp >> 12) & 0x0fu));
    enc[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
    enc[2] = (unsigned char)(0x80u | (cp & 0x3fu));
    n      = 3;
  } else {
    return -1;
  }

  if(*pos + (size_t)n >= size)
    return -1;
  for(int i = 0; i < n; i++) {
    buf[(*pos)++] = (char)enc[i];
    sh_putchar((char)enc[i]);
  }
  return 0;
}

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
    if(s_edit.pushback >= 0) {
      c               = s_edit.pushback;
      s_edit.pushback = -1;
    } else {
      if(sh_fb_tty_active())
        c = sh_getchar_blinking(400);
      else
        c = sh_getchar();
    }
    if(c < 0)
      continue;

    if(c == '\n' || c == '\r') {
      sh_putchar('\n');
      buf[pos] = '\0';
      line_edit_reset(&s_edit);
      return (int)pos;
    }

    if(c == '\b' || c == 127) {
      if(pos > 0) {
        size_t np = pos - 1;
        while(np > 0 && ((unsigned char)buf[np] & 0xc0u) == 0x80u)
          np--;
        pos = np;
        sh_puts("\b \b");
      }
      s_edit.utf8_rem = 0;
      fb_cursor_after_edit();
      continue;
    }

    if(c == 0x03) {
      sh_puts("^C\n");
      buf[0] = '\0';
      line_edit_reset(&s_edit);
      return 0;
    }

    if(c == 0x04) {
      if(pos == 0) {
        s_edit.pushback = -1;
        s_edit.utf8_rem = 0;
        return -1;
      }
      continue;
    }

    if(c == 0x0C) {
      sh_clear();
      fb_cursor_after_edit();
      line_edit_reset(&s_edit);
      return 0;
    }

    /* ESC: drain CSI/SS3 sequences so their bytes do not land in the buffer. */
    if(c == 0x1b) {
      s_edit.utf8_rem = 0;
      int c2          = sh_getchar();
      if(c2 < 0)
        continue;
      if(c2 == '[') {
        /* CSI: param bytes are 0x30–0x3f; the first byte outside that range
         * is the final byte (letter or '~'). Covers arrows, Delete, Home… */
        int cp;
        do {
          cp = sh_getchar();
        } while(cp >= 0x30 && cp <= 0x3f);
        (void)cp;
        continue;
      }
      if(c2 == 'O') {
        /* SS3: exactly one final byte (F1–F4 on some terminals). */
        (void)sh_getchar();
        continue;
      }
      s_edit.pushback = c2;
      while(pos > 0) {
        size_t np = pos - 1;
        while(np > 0 && ((unsigned char)buf[np] & 0xc0u) == 0x80u)
          np--;
        pos = np;
        sh_puts("\b \b");
      }
      fb_cursor_after_edit();
      continue;
    }

    unsigned char u = (unsigned char)c;

    if(s_edit.utf8_rem > 0) {
      if((u & 0xc0u) != 0x80u) {
        /* Bad continuation — restart with this byte. */
        s_edit.utf8_rem = 0;
        s_edit.pushback = (int)u;
        continue;
      }
      s_edit.utf8_partial = (s_edit.utf8_partial << 6u) | (uint32_t)(u & 0x3fu);
      s_edit.utf8_rem--;
      if(s_edit.utf8_rem != 0)
        continue;
      if(s_edit.utf8_partial <= 0x10ffffu &&
         store_utf8_cp(buf, size, &pos, s_edit.utf8_partial) < 0) {
        s_edit.utf8_partial = 0;
      }
      fb_cursor_after_edit();
      continue;
    }

    /* ASCII printable */
    if(u < 0x80u) {
      if(u >= 32u && u < 127u && pos < size - 1) {
        buf[pos++] = (char)u;
        sh_putchar((char)u);
        fb_cursor_after_edit();
      }
      continue;
    }

    /* UTF-8 multibyte lead byte */
    if((u & 0xe0u) == 0xc0u) {
      s_edit.utf8_partial = (uint32_t)(u & 0x1fu);
      s_edit.utf8_rem     = 1;
    } else if((u & 0xf0u) == 0xe0u) {
      s_edit.utf8_partial = (uint32_t)(u & 0x0fu);
      s_edit.utf8_rem     = 2;
    } else if((u & 0xf8u) == 0xf0u) {
      s_edit.utf8_partial = (uint32_t)(u & 0x07u);
      s_edit.utf8_rem     = 3;
    } else {
      /* Latin-1 single byte from kbd layout (e.g. é → 0xE9) */
      (void)store_utf8_cp(buf, size, &pos, (uint32_t)u);
      fb_cursor_after_edit();
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

#define MAX_HEREDOC_DELIM 64

/* True if @p buf parses as a complete statement: no open quote, balanced
 * braces, no pending heredoc body. Mirrors the lexer's quoting rules —
 * single quotes are literal, double quotes recognise \" \\ \$ as escapes.
 * Brace counting is suppressed inside either quote.
 *
 * Heredoc tracking: when `<<` (not `<<<`) appears outside quotes, the next
 * word is captured as the delimiter; from the following newline onward the
 * walker watches each line for an exact match against the delimiter, and
 * stays incomplete until found. We support one heredoc per command for now.
 * A negative brace depth (`}` without `{`) is treated as complete: let the
 * parser surface the error rather than wedge the REPL. */
static int is_input_complete(const char *buf)
{
  int  in_squote   = 0;
  int  in_dquote   = 0;
  int  brace_depth = 0;
  int  want_delim  = 0; /* saw `<<`, scanning for delim word */
  int  in_hd_body  = 0; /* between `<<DELIM\n` and the closing line */
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
        /* line longer than max delim — can't match */
        ln = MAX_HEREDOC_DELIM - 1;
      }
      continue;
    }

    if(want_delim) {
      if(c == ' ' || c == '\t') {
        if(dn > 0) {
          /* delim ended */
          want_delim = 0;
        }
        continue;
      }
      if(c == '\n') {
        if(dn > 0) {
          want_delim = 0;
          in_hd_body = 1;
          ln         = 0;
        }
        /* if dn == 0, no delim yet — stay in want_delim, more input needed */
        continue;
      }
      if(dn < MAX_HEREDOC_DELIM - 1) {
        delim[dn++] = c;
      }
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
        /* `<<<` — herestring, not a heredoc. Skip all three so we don't
         * re-detect `<<` on the next iteration. */
        p += 2;
        continue;
      }
      want_delim = 1;
      dn         = 0;
      p++; /* skip the second '<' */
      continue;
    }
    if(c == '\n') {
      if(dn > 0) {
        /* a delim was captured earlier on this line; switch to body now */
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
    fb_cursor_after_edit();
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

  /* No procfs on Alcor2; LLVM/musl fall back to PATH for argv-only lookups. */
  setenv("PATH", "/bin:/usr/bin", 0);

  /* Raw mode: shell handles its own line editing and echo, character by
   * character. */
  sh_set_stdin_raw();

  const char *font = getenv("ALCOR2_FONT");
  if(!font || !*font)
    font = "/bin/FiraCode-Regular.ttf";
  const char *fb_off = getenv("ALCOR2_FB_TTY");
  if(!(fb_off && fb_off[0] == '0'))
    (void)sh_fb_tty_init(font);

  char line[MAX_CMD_LEN];

  sh_puts("\n");
  sh_puts("  vega " VEGA_VERSION " - Alcor2 shell\n");
  sh_puts("  Type 'help' for available commands.\n");
  sh_puts("\n");

  while(1) {
    print_prompt();
    fb_cursor_after_edit();

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
