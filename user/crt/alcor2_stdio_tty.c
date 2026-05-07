/* Unbuffer stdin/stdout on TTYs so prompts and getchar cooperate; leave
 * pipes/files buffered.
 *
 * Ncurses (and anything using terminfo) needs a real TERM= name. Kernels often
 * leave TERM unset or set it to "unknown", which yields:
 *   Error opening terminal: unknown
 * Default matches our ncurses fallbacks + QEMU serial/console behaviour. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__attribute__((constructor)) static void alcor2_stdio_tty_buffering(void)
{
  const char *t = getenv("TERM");
  if(!t || !t[0] || !strcmp(t, "unknown"))
    setenv("TERM", "xterm-256color", 1);

  if(isatty(STDIN_FILENO))
    setvbuf(stdin, NULL, _IONBF, 0);
  if(isatty(STDOUT_FILENO))
    setvbuf(stdout, NULL, _IONBF, 0);
}
