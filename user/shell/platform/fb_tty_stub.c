/**
 * @file user/shell/platform/fb_tty_stub.c
 * @brief No-op framebuffer TTY when FreeType/HarfBuzz/musl-g++ are not built.
 */

#include <vega/fb_tty.h>

bool sh_fb_tty_init(const char *font_path)
{
  (void)font_path;
  return false;
}

bool sh_fb_tty_active(void)
{
  return false;
}

void sh_fb_tty_on_fork_child(void) {}

void sh_fb_tty_shutdown(void) {}

void sh_fb_tty_puts(const char *s)
{
  (void)s;
}

void sh_fb_tty_putchar(unsigned char c)
{
  (void)c;
}

void sh_fb_tty_present(void) {}

void sh_fb_tty_clear(void) {}

void sh_fb_tty_cursor_poll(void) {}

void sh_fb_tty_blink_tick(void) {}

void sh_fb_tty_cursor_suspend(void) {}

void sh_fb_tty_cursor_after_edit(void) {}

void sh_fb_tty_flush(void) {}
