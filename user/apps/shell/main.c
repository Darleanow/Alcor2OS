#include <curses.h>
#include <shell/atlas.h>
#include <shell/shell.h>
#include <spazer/spazer.h>
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

static int utf8_cols(const char *s)
{
  int n = 0;
  for(; *s; s++)
    if(((unsigned char)*s & 0xC0u) != 0x80u)
      n++;
  return n;
}

/* Counts visible columns in a string, skipping ANSI ESC[...m sequences. */
static int visible_cols(const char *s)
{
  int n = 0;
  while(*s) {
    if((unsigned char)*s == 0x1b) {
      s++;
      if(*s == '[') {
        s++;
        while(*s && !(*s >= '@' && *s <= '~'))
          s++;
        if(*s)
          s++;
      } else if(*s) {
        s++;
      }
    } else if(((unsigned char)*s & 0xC0u) != 0x80u) {
      n++;
      s++;
    } else {
      s++;
    }
  }
  return n;
}

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
  int prompt_cols = visible_cols(prompt);
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

/* Prompt colours — semantic theme roles. */
#define PC_LINE THEME_ANSI_DIM
#define PC_HOST THEME_ANSI_ACCENT_B
#define PC_PATH THEME_ANSI_SUBTEXT
#define PC_DOLS THEME_ANSI_SUCCESS_B
#define PC_RS   THEME_ANSI_RESET

/* Box-drawing; rounded corners are arc-rasterised in atlas.c. */
#define PC_TL BD_TL_R /* ╭ */
#define PC_BL BD_BL_R /* ╰ */
#define PC_H  BD_H    /* ─ */

static void write_prompt_header(void)
{
  char        cwd[MAX_PATH];
  const char *path = sh_getcwd(cwd, sizeof cwd) ? cwd : "/";
  write_str(PC_LINE PC_TL PC_H " " PC_RS); /* ╭─ space */
  write_str(PC_HOST "alcor2" PC_RS);
  write_str(PC_LINE " " PC_H " " PC_RS); /* space ─ space */
  write_str(PC_PATH);
  write_str(path);
  write_str(PC_RS "\n");
}

static void format_prompt(char *out, size_t cap)
{
  snprintf(
      out, cap,
      PC_LINE PC_BL PC_H " " PC_RS /* ╰─ space */
      PC_DOLS "$" PC_RS " "
  ); /* $ space  */
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

  /* Static bottom-line prompt — same every time: ╰─ $  */
  char prompt[128];
  format_prompt(prompt, sizeof prompt);

  /* Continuation prompt for multi-line input: │ »  (indented) */
  char cont_prompt[64];
  snprintf(
      cont_prompt, sizeof cont_prompt,
      PC_LINE BD_V " " PC_RS /* │ space */
      PC_DOLS "\xc2\xbb" PC_RS " "
  ); /* »  (U+00BB Latin-1) */

  const char *cur_prompt = prompt;

  while(1) {
    /* Top decorative line only for primary prompt, not continuation. */
    if(cur_prompt == prompt)
      write_prompt_header();

    int len = read_line(buf + pos, size - pos, cur_prompt);
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

#define BNR_H34                                                                \
  BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H   \
      BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H BD_H    \
          BD_H BD_H BD_H BD_H BD_H
  write_str(
      "\n"
      "  " PC_LINE BD_TL_R BNR_H34 BD_TR_R PC_RS "\n"
      "  " PC_LINE BD_V PC_RS "            " PC_HOST "ALCOR2  OS" PC_RS
      "            " PC_LINE     BD_V PC_RS "\n"
      "  " PC_LINE BD_LT BNR_H34 BD_RT PC_RS "\n"
      "  " PC_LINE BD_V PC_RS "           " PC_PATH "vega v" VEGA_VERSION PC_RS
      "            " PC_LINE       BD_V PC_RS "\n"
      "  " PC_LINE BD_BL_R BNR_H34 BD_BR_R PC_RS "\n"
      "\n"
      "  " PC_DOLS "help" PC_LINE " " BD_ARROW_R " " PC_RS PC_PATH
      "list available commands" PC_RS "\n\n"
  );
#undef BNR_H34

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
