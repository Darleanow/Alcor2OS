/**
 * @file apps/shell/platform/io.c
 * @brief Shell I/O — thin musl wrappers around fd 0/1.
 *
 * Output goes through the kernel's fb_console (cell grid + ANSI/CSI parser
 * + atlas rendering), so userspace doesn't intercept or render anything.
 */

#include <shell/shell.h>
#include <string.h>
#include <unistd.h>

void sh_putchar(char c)
{
  sh_write(STDOUT_FILENO, &c, 1);
}

void sh_puts(const char *s)
{
  sh_write(STDOUT_FILENO, s, strlen(s));
}

void sh_stdout_bytes(const void *buf, size_t len)
{
  if(len)
    sh_write(STDOUT_FILENO, buf, len);
}

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
    buf[i++] = (char)('0' + (int)(n % 10));
    n /= 10;
  }
  if(neg)
    sh_putchar('-');
  while(i > 0)
    sh_putchar(buf[--i]);
}
