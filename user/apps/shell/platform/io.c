/**
 * @file apps/shell/platform/io.c
 * @brief Minimal I/O for the shell (line read/write, prompts).
 */

#include <shell/fb_tty.h>
#include <shell/shell.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/**
 * @brief Write a single character to stdout
 * @param c Character to write
 */
void sh_putchar(char c)
{
  if(sh_fb_tty_active()) {
    sh_fb_tty_putchar((unsigned char)c);
    sh_fb_tty_flush(
    ); /* reshape immediately so char is visible; no yield (interactive) */
    return;
  }
  sh_write(STDOUT_FILENO, &c, 1);
}

/**
 * @brief Write a string to stdout
 * @param s Null-terminated string to write
 */
void sh_puts(const char *s)
{
  if(sh_fb_tty_active()) {
    sh_fb_tty_puts(s);
    sh_fb_tty_present();
    return;
  }
  sh_write(STDOUT_FILENO, s, strlen(s));
}

void sh_stdout_bytes(const void *buf, size_t len)
{
  if(!len)
    return;
  if(sh_fb_tty_active()) {
    const unsigned char *p = (const unsigned char *)buf;
    for(size_t i = 0; i < len; i++)
      sh_fb_tty_putchar(p[i]);
    sh_fb_tty_present();
    return;
  }
  sh_write(STDOUT_FILENO, buf, len);
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
    buf[i++] = (char)('0' + (int)(n % 10));
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

int sh_getchar_blinking(int idle_ms)
{
  if(!sh_fb_tty_active() || idle_ms <= 0)
    return sh_getchar();

  for(;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv;
    tv.tv_sec  = (long)(idle_ms / 1000);
    tv.tv_usec = (long)((idle_ms % 1000) * 1000);
    int r      = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if(r < 0)
      return -1;
    if(r == 0) {
      /* cursor_poll owns s_blink_show_bar; blink_tick consults it when it
       * restores the bar, so the prompt cursor stays in sync regardless of
       * which rows the A_BLINK pass repaints. */
      sh_fb_tty_cursor_poll();
      sh_fb_tty_blink_tick();
      continue;
    }
    if(!FD_ISSET(STDIN_FILENO, &rfds))
      continue;
    sh_fb_tty_cursor_suspend();
    return sh_getchar();
  }
}
