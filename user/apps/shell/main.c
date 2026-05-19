/**
 * @file apps/shell/main.c
 * @brief vega REPL with an ncurses-backed line editor.
 *
 * ncurses is used for input only: getch() with keypad mode gives us KEY_UP,
 * KEY_LEFT, KEY_HOME, KEY_F(n)… without us having to parse escape sequences.
 * Output is written straight to stdout (the kernel fb_console speaks ANSI
 * cleanly) so child process stdout, prompts, and history redraws all share
 * the same scrollable stream — ncurses' internal screen model never gets a
 * chance to diverge from the real terminal.
 */

#include <curses.h>
#include <shell/atlas.h>
#include <shell/shell.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <vega/host.h>
#include <vega/vega.h>

static const vega_host_ops_t shell_host = {
    .is_builtin  = sh_is_builtin,
    .run_builtin = sh_run_builtin,
};

#ifndef VEGA_VERSION
  #define VEGA_VERSION "1.0.0"
#endif

#define HIST_MAX     128
#define LINE_MAX_LEN MAX_CMD_LEN

/** Special return codes for read_line(). Negative so they can't be confused
 *  with a byte count. */
#define RL_EOF       (-1)
#define RL_INTERRUPT (-2)
#define RL_CLEAR     (-3)

/* History ring. Oldest entry is at index 0; newest at hist_count-1. UP/DOWN
 * navigate a transient cursor (hist_view) into this list while editing. */
static char history[HIST_MAX][LINE_MAX_LEN];
static int  hist_count = 0;

static void hist_push(const char *line)
{
  if(!line || !line[0])
    return;
  /* Skip if identical to the most recent entry. */
  if(hist_count > 0 && strcmp(history[hist_count - 1], line) == 0)
    return;
  if(hist_count == HIST_MAX) {
    memmove(history[0], history[1], sizeof(history[0]) * (HIST_MAX - 1));
    hist_count--;
  }
  strncpy(history[hist_count], line, LINE_MAX_LEN - 1);
  history[hist_count][LINE_MAX_LEN - 1] = '\0';
  hist_count++;
}

static void write_str(const char *s)
{
  size_t n = strlen(s);
  while(n > 0) {
    ssize_t w = write(STDOUT_FILENO, s, n);
    if(w <= 0)
      return;
    s += w;
    n -= (size_t)w;
  }
}

/** Erase the current input line and reprint @p prompt + @p buf; leave the
 *  terminal cursor at logical column (prompt_cols + cur_cols). */
static void redraw_line(
    const char *prompt, int prompt_cols, const char *buf, int cur_cols
)
{
  char tail[16];
  /* \r → col 0; \033[K → clear to end of line. */
  write_str("\r\033[K");
  write_str(prompt);
  write_str(buf);
  /* CHA — Cursor Horizontal Absolute (1-based). */
  snprintf(tail, sizeof tail, "\r\033[%dC", prompt_cols + cur_cols);
  if(cur_cols + prompt_cols > 0)
    write_str(tail);
  else
    write_str("\r");
}

/** Count visible columns in a UTF-8 byte string by ignoring continuation
 *  bytes. Each non-continuation byte represents one logical character. */
static int utf8_cols(const char *s)
{
  int n = 0;
  for(; *s; s++)
    if(((unsigned char)*s & 0xC0u) != 0x80u)
      n++;
  return n;
}

/** Step a byte index back/forward by one UTF-8 character boundary. */
static int prev_char_boundary(const char *buf, int idx)
{
  if(idx <= 0)
    return 0;
  idx--;
  while(idx > 0 && ((unsigned char)buf[idx] & 0xC0u) == 0x80u)
    idx--;
  return idx;
}

static int next_char_boundary(const char *buf, int len, int idx)
{
  if(idx >= len)
    return len;
  idx++;
  while(idx < len && ((unsigned char)buf[idx] & 0xC0u) == 0x80u)
    idx++;
  return idx;
}

/* Off-screen pad used purely as a getch() source. Reading from a pad (rather
 * than stdscr) avoids ncurses' implicit wrefresh on the visible screen — we
 * don't want it overwriting our prompts and child stdout with blanks. */
static WINDOW *s_input_pad;

/* Termios states swapped around child execs. `raw_t` is what ncurses set up
 * (non-canonical, no echo, no signals); `cooked_t` is the same with ICANON +
 * ECHO + ISIG re-enabled so unredirected children like `cat` see line-buffered
 * input and Ctrl-D triggers EOF. Set in main() right after ncurses init. */
static struct termios s_raw_t;
static struct termios s_cooked_t;

/* Read a single line. @p buf is filled with bytes (no trailing newline) and
 * null-terminated; the byte count is returned. RL_EOF on Ctrl-D at empty
 * line, RL_INTERRUPT on Ctrl-C, RL_CLEAR on Ctrl-L (caller redraws). */
static int read_line(char *buf, size_t cap, const char *prompt)
{
  int prompt_cols = utf8_cols(prompt);
  int len         = 0; /* bytes in buf */
  int cur_b       = 0; /* byte index of cursor */
  int hist_view   = hist_count;
  buf[0]          = '\0';

  write_str(prompt);

  for(;;) {
    int c = wgetch(s_input_pad);
    if(c == ERR)
      continue;

    switch(c) {
    case '\n':
    case '\r':
    case KEY_ENTER:
      write_str("\n");
      buf[len] = '\0';
      return len;

    case 0x7f:
    case '\b':
    case KEY_BACKSPACE: {
      if(cur_b == 0)
        break;
      int prev = prev_char_boundary(buf, cur_b);
      memmove(buf + prev, buf + cur_b, (size_t)(len - cur_b + 1));
      len -= (cur_b - prev);
      cur_b = prev;
      redraw_line(
          prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
      );
      break;
    }

    case KEY_DC: {
      if(cur_b >= len)
        break;
      int nx = next_char_boundary(buf, len, cur_b);
      memmove(buf + cur_b, buf + nx, (size_t)(len - nx + 1));
      len -= (nx - cur_b);
      redraw_line(
          prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
      );
      break;
    }

    case KEY_LEFT:
      if(cur_b > 0) {
        cur_b = prev_char_boundary(buf, cur_b);
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;

    case KEY_RIGHT:
      if(cur_b < len) {
        cur_b = next_char_boundary(buf, len, cur_b);
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;

    case KEY_HOME:
      cur_b = 0;
      redraw_line(prompt, prompt_cols, buf, 0);
      break;

    case KEY_END:
      cur_b = len;
      redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
      break;

    case KEY_UP:
      if(hist_view > 0) {
        hist_view--;
        strncpy(buf, history[hist_view], cap - 1);
        buf[cap - 1] = '\0';
        len          = (int)strlen(buf);
        cur_b        = len;
        redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
      }
      break;

    case KEY_DOWN:
      if(hist_view < hist_count) {
        hist_view++;
        if(hist_view == hist_count) {
          buf[0] = '\0';
          len    = 0;
          cur_b  = 0;
        } else {
          strncpy(buf, history[hist_view], cap - 1);
          buf[cap - 1] = '\0';
          len          = (int)strlen(buf);
          cur_b        = len;
        }
        redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
      }
      break;

    case 0x03: /* Ctrl-C */
      write_str("^C\n");
      buf[0] = '\0';
      return RL_INTERRUPT;

    case 0x04: /* Ctrl-D */
      if(len == 0)
        return RL_EOF;
      break;

    case 0x0C: /* Ctrl-L */
      return RL_CLEAR;

    default:
      /* Insert byte at cur_b. ASCII printables (0x20..0x7E) and UTF-8 bytes
       * (0x80..0xFF) are kept; everything else is ignored. KEY_* values are
       * all >= KEY_MIN (0x101), which falls outside our printable range. */
      if(c < 0x100 && (c == ' ' || (c >= 0x21 && c <= 0x7e) || c >= 0x80)) {
        if(len + 1 >= (int)cap - 1)
          break;
        memmove(buf + cur_b + 1, buf + cur_b, (size_t)(len - cur_b + 1));
        buf[cur_b++] = (char)c;
        len++;
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;
    }
  }
}

/**
 * @brief Print the shell prompt into a buffer (instead of writing immediately
 * so the prompt-length stays in sync with what read_line draws).
 */
static void format_prompt(char *out, size_t cap)
{
  char cwd[MAX_PATH];
  if(sh_getcwd(cwd, sizeof cwd))
    snprintf(out, cap, "alcor2:%s$ ", cwd);
  else
    snprintf(out, cap, "alcor2> ");
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

/* Read input lines into @p buf until they form a complete statement.
 * Returns total bytes accumulated, RL_EOF on Ctrl-D at empty line, or 0
 * when interrupted with Ctrl-C. */
static int read_complete_statement(char *buf, size_t size)
{
  size_t pos = 0;
  buf[0]     = '\0';

  char prompt[MAX_PATH + 16];
  format_prompt(prompt, sizeof prompt);

  while(1) {
    int len = read_line(buf + pos, size - pos, prompt);
    if(len == RL_EOF)
      return RL_EOF;
    if(len == RL_INTERRUPT)
      return 0;
    if(len == RL_CLEAR) {
      sh_clear();
      /* Re-prompt with whatever has already been accumulated on prior lines.
       * Most often pos == 0 and the user just wanted a clean screen. */
      continue;
    }

    pos += (size_t)len;
    if(pos >= size - 2)
      return (int)pos;

    buf[pos++] = '\n';
    buf[pos]   = '\0';

    if(is_input_complete(buf))
      return (int)pos;

    /* Continuation prompt — narrower than the primary prompt so it's
     * visually distinct. */
    snprintf(prompt, sizeof prompt, "> ");
  }
}

/**
 * @brief Shell main entry point. Sets up libvega, submits the Fira atlas,
 * initialises ncurses for input handling, then enters the REPL.
 */
int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  vega_init(&shell_host);
  setenv("PATH", "/bin:/usr/bin", 0);

  const char *font = getenv("ALCOR2_FONT");
  if(!font || !*font)
    font = "/bin/FiraCode-Regular.ttf";
  const char *fb_off = getenv("ALCOR2_FB_TTY");
  if(!(fb_off && fb_off[0] == '0'))
    (void)atlas_submit(font);

  /* ncurses: input-only. We never call refresh/addstr — output goes through
   * write(STDOUT_FILENO, …) so child stdout and prompt drawing share the
   * same scrollable stream. */
  const char *term = getenv("TERM");
  if(!term || !*term)
    term = "xterm-256color";
  SCREEN *scr = newterm(term, stdout, stdin);
  if(!scr) {
    write_str("shell: newterm failed; falling back to raw stdio.\n");
    return 1;
  }
  set_term(scr);
  raw();
  noecho();
  nonl();
  /* Off-screen pad: reading from it lets us use ncurses' keypad escape
   * parsing without ever triggering a screen refresh that would clobber our
   * write()-based output. */
  s_input_pad = newpad(1, 256);
  if(!s_input_pad) {
    write_str("shell: newpad failed\n");
    endwin();
    delscreen(scr);
    return 1;
  }
  keypad(s_input_pad, TRUE);
  intrflush(s_input_pad, FALSE);

  /* Snapshot the raw termios ncurses just configured, then build a cooked
   * variant for handing off to child processes. */
  if(tcgetattr(STDIN_FILENO, &s_raw_t) == 0) {
    s_cooked_t = s_raw_t;
    s_cooked_t.c_lflag |= (tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    s_cooked_t.c_iflag |= (tcflag_t)(ICRNL);
    s_cooked_t.c_oflag |= (tcflag_t)(OPOST | ONLCR);
  }

  char line[LINE_MAX_LEN];

  write_str("\n  vega " VEGA_VERSION " - Alcor2 shell\n");
  write_str("  Type 'help' for available commands.\n\n");

  while(1) {
    int len = read_complete_statement(line, sizeof line);
    if(len == RL_EOF) {
      write_str("exit\n");
      break;
    }
    if(len > 0) {
      /* Strip the trailing newline read_complete_statement appended so the
       * history entry looks like what the user typed. */
      size_t tlen = (size_t)len;
      while(tlen > 0 && line[tlen - 1] == '\n')
        line[--tlen] = '\0';
      hist_push(line);

      /* Restore canonical termios before forking children so things like
       * `cat` with no args can be terminated with Ctrl-D (VEOF only fires
       * in canonical mode). We bypass ncurses' endwin/reset_prog_mode pair
       * because those re-emit terminfo init strings on every iteration,
       * scrolling the screen and slowing things down. Direct tcsetattr is
       * a no-op on the framebuffer — it just toggles input-side flags. */
      tcsetattr(STDIN_FILENO, TCSANOW, &s_cooked_t);
      vega_run(line);
      tcsetattr(STDIN_FILENO, TCSANOW, &s_raw_t);
      /* A child running its own ncurses session (ncurses-hello) calls
       * endwin() on exit, which sends rmkx (`\E[?1l\E>`) and turns DECCKM
       * off in the kernel. The shell's keypad mode is still on, so the
       * next arrow press would arrive as CSI instead of SS3 — UP/DOWN/etc.
       * would stop translating to KEY_*. Re-assert DECCKM-on here so the
       * kernel emits SS3 again. */
      (void)write(STDOUT_FILENO, "\033[?1h", 5);
    }
  }

  endwin();
  delscreen(scr);
  return 0;
}
