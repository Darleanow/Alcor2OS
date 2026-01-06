/**
 * Alcor2 Shell - I/O Utilities
 */

#include "shell.h"
#include <unistd.h>

/**
 * @brief Write a single character to stdout
 * @param c Character to write
 */
void sh_putchar(char c)
{
  sh_write(STDOUT_FILENO, &c, 1);
}

/**
 * @brief Write a string to stdout
 * @param s Null-terminated string to write
 */
void sh_puts(const char *s)
{
  sh_write(STDOUT_FILENO, s, sh_strlen(s));
}

/**
 * @brief Write a decimal number to stdout
 * @param n Number to write
 */
void sh_putnum(long n)
{
  char buf[32];
  int  i   = 0;
  int  neg = 0;

  if(n < 0) {
    neg = 1;
    n   = -n;
  }

  if(n == 0) {
    sh_putchar('0');
    return;
  }

  while(n > 0) {
    buf[i++] = '0' + (n % 10);
    n /= 10;
  }

  if(neg)
    sh_putchar('-');

  while(i > 0) {
    sh_putchar(buf[--i]);
  }
}

/**
 * @brief Read a single character from stdin
 * @return Character read (0-255), or -1 on error/EOF
 */
int sh_getchar(void)
{
  char c;
  if(sh_read(STDIN_FILENO, &c, 1) <= 0) {
    return -1;
  }
  return (unsigned char)c;
}
