/**
 * @file main.c
 * @brief Shell entry point: bring up ncurses input, atlas, termios snapshot,
 *        source @c /home/.vconf, then run the REPL forever (read a complete
 *        vega statement, hand it to @ref vega_run, restart).
 *
 * The actual line editor lives in @c platform/edit.c, history in
 * @c platform/history.c, prompt rendering in @c platform/prompt.c, the
 * brace/quote/heredoc-aware statement reader in @c platform/parse.c, and the
 * config-file sourcing in @c platform/vconf.c. This file just sequences them.
 */

#include <boxdraw.h>
#include <curses.h>
#include <shell/atlas.h>
#include <shell/shell.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <theme.h>
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

#define LINE_MAX_LEN MAX_CMD_LEN

/* Termios states swapped around child execs. `raw_t` is what ncurses set up
 * (non-canonical, no echo, no signals); `cooked_t` is the same with ICANON +
 * ECHO + ISIG re-enabled so unredirected children like `cat` see line-buffered
 * input and Ctrl-D triggers EOF. */
static struct termios s_raw_t;
static struct termios s_cooked_t;

int                   main(int argc, char *argv[])
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
    sh_puts("shell: newterm failed; falling back to raw stdio.\n");
    return 1;
  }
  set_term(scr);
  raw();
  noecho();
  nonl();

  if(sh_edit_init() < 0) {
    sh_puts("shell: newpad failed\n");
    endwin();
    delscreen(scr);
    return 1;
  }

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
  sh_puts(
      "\n"
      "  " THEME_ANSI_DIM BD_TL_R BNR_H34 BD_TR_R THEME_ANSI_RESET "\n"
      "  " THEME_ANSI_DIM BD_V THEME_ANSI_RESET
      "            " THEME_ANSI_ACCENT_B "ALCOR2  OS" THEME_ANSI_RESET
      "            " THEME_ANSI_DIM BD_V THEME_ANSI_RESET "\n"
      "  " THEME_ANSI_DIM BD_LT BNR_H34 BD_RT THEME_ANSI_RESET "\n"
      "  " THEME_ANSI_DIM BD_V THEME_ANSI_RESET "           " THEME_ANSI_SUBTEXT
      "vega v" VEGA_VERSION THEME_ANSI_RESET
      "            " THEME_ANSI_DIM BD_V THEME_ANSI_RESET "\n"
      "  " THEME_ANSI_DIM BD_BL_R BNR_H34 BD_BR_R THEME_ANSI_RESET "\n"
      "\n"
      "  " THEME_ANSI_SUCCESS_B "help" THEME_ANSI_DIM " " BD_ARROW_R
      " " THEME_ANSI_RESET THEME_ANSI_SUBTEXT
      "list available commands" THEME_ANSI_RESET "\n\n"
  );
#undef BNR_H34

  /* Source /home/.vconf if it exists — equivalent of .bashrc. Runs as a vega
   * script so any language feature (let, if, fn, kbd, …) works. tcsetattr is
   * required so children inside the script see canonical mode. */
  tcsetattr(STDIN_FILENO, TCSANOW, &s_cooked_t);
  sh_source_vconf("/home/.vconf");
  tcsetattr(STDIN_FILENO, TCSANOW, &s_raw_t);

  while(1) {
    int len = sh_read_complete_statement(line, sizeof line);
    if(len == RL_EOF) {
      sh_puts("exit\n");
      break;
    }
    if(len > 0) {
      /* Strip the trailing newline read_complete_statement appended so the
       * history entry looks like what the user typed. */
      size_t tlen = (size_t)len;
      while(tlen > 0 && line[tlen - 1] == '\n')
        line[--tlen] = '\0';
      sh_hist_push(line);

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
