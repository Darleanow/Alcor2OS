/* Stdio buffering for Alcor2 userland.
 *
 * - TTY stdin: unbuffered so getchar/wgetch sees keys without line delay.
 * - TTY or pipe stdout: when the shell relays child stdout through a pipe into
 *   the FB terminal, isatty(1) is false and musl uses *full* buffering.
 *   ncurses refresh() then keeps CSI output in memory until exit — blank
 *   screen until getch returns. Force unbuffered stdout (and stderr) so every
 *   write reaches the relay immediately.
 *
 * Trade-off: redirections like `cmd > bigfile` do more syscalls; acceptable
 * on this system.
 *
 * Ncurses needs a real TERM= name. Kernels often leave TERM unset or
 * "unknown", which yields: Error opening terminal: unknown */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__attribute__((constructor)) static void alcor2_stdio_tty_buffering(void)
{
  const char *t = getenv("TERM");
  if(!t || !t[0] || !strcmp(t, "unknown"))
    setenv("TERM", "xterm-256color", 1);

  /* ncurses looks here before the compiled-in default; guest must ship terminfo. */
  if(!getenv("TERMINFO"))
    setenv("TERMINFO", "/usr/share/terminfo", 0);

  if(isatty(STDIN_FILENO))
    setvbuf(stdin, NULL, _IONBF, 0);

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
}
