/* Unbuffer stdin/stdout on TTYs so prompts and getchar cooperate; leave
 * pipes/files buffered. */
#include <stdio.h>
#include <unistd.h>

__attribute__((constructor)) static void alcor2_stdio_tty_buffering(void)
{
  if(isatty(STDIN_FILENO))
    setvbuf(stdin, NULL, _IONBF, 0);
  if(isatty(STDOUT_FILENO))
    setvbuf(stdout, NULL, _IONBF, 0);
}
