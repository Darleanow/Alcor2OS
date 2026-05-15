/**
 * @file apps/vega/host.c
 * @brief libvega host implementation for the non-interactive vega CLI.
 *
 * No framebuffer terminal, no line editing: every sh_* call delegates to
 * musl. The three fb_tty host hooks are no-ops.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vega/host.h>

void sh_putchar(char c)
{
  write(STDOUT_FILENO, &c, 1);
}

void sh_puts(const char *s)
{
  write(STDOUT_FILENO, s, strlen(s));
}

void sh_putnum(long n)
{
  char buf[32];
  int  len = snprintf(buf, sizeof buf, "%ld", n);
  if(len > 0)
    write(STDOUT_FILENO, buf, (size_t)len);
}

void sh_stdout_bytes(const void *buf, size_t len)
{
  if(len)
    write(STDOUT_FILENO, buf, len);
}

int sh_getchar(void)
{
  char c;
  if(read(STDIN_FILENO, &c, 1) <= 0)
    return -1;
  return (unsigned char)c;
}

int sh_getchar_blinking(int idle_ms)
{
  (void)idle_ms;
  return sh_getchar();
}

void sh_exit(int code)
{
  exit(code);
}

void sh_set_stdin_raw(void)
{
  struct termios t;
  if(tcgetattr(STDIN_FILENO, &t) != 0)
    return;
  t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
  t.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
  t.c_cc[VMIN]  = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

long sh_read(int fd, void *buf, size_t len)
{
  return read(fd, buf, len);
}

long sh_write(int fd, const void *buf, size_t len)
{
  return write(fd, buf, len);
}

int sh_ioctl(int fd, unsigned long request, void *arg)
{
  return ioctl(fd, (int)request, arg);
}

void sh_clear(void)
{
  const char *clear = "\033[2J\033[H";
  write(STDOUT_FILENO, clear, strlen(clear));
}

int sh_open(const char *path, int flags)
{
  return open(path, flags);
}

int sh_close(int fd)
{
  return close(fd);
}

DIR *sh_opendir(const char *path)
{
  return opendir(path);
}

struct dirent *sh_readdir(DIR *dir)
{
  return readdir(dir);
}

int sh_closedir(DIR *dir)
{
  return closedir(dir);
}

int sh_stat(const char *path, struct stat *st)
{
  return stat(path, st);
}

int sh_mkdir(const char *path)
{
  return mkdir(path, 0755);
}

int sh_chdir(const char *path)
{
  return chdir(path);
}

char *sh_getcwd(char *buf, size_t size)
{
  return getcwd(buf, size);
}

int sh_unlink(const char *path)
{
  return unlink(path);
}

/* FB TTY host hooks: never active in the CLI. */
bool sh_fb_tty_active(void)
{
  return false;
}

void sh_fb_tty_on_fork_child(void) {}

void sh_fb_tty_blink_tick(void) {}

/* Host builtin hooks: the CLI has no shell-UX commands. */
bool sh_is_builtin(const char *name)
{
  (void)name;
  return false;
}

int sh_run_builtin(int argc, char *const argv[])
{
  (void)argc;
  (void)argv;
  return -1;
}
