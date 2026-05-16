/**
 * @file apps/vega/host.c
 * @brief libvega host implementation for the non-interactive vega CLI.
 *
 * Static instance of vega_host_ops_t wired to plain musl. No FB TTY, no
 * line editor, no host-side builtins — the CLI is a stateless interpreter.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vega/host.h>

static void host_puts(const char *s)
{
  (void)write(STDOUT_FILENO, s, strlen(s));
}

static void host_stdout_bytes(const void *buf, size_t len)
{
  if(len)
    (void)write(STDOUT_FILENO, buf, len);
}

static int host_close(int fd)
{
  return close(fd);
}

static long host_read(int fd, void *buf, size_t len)
{
  return read(fd, buf, len);
}

static int host_stat(const char *path, struct stat *st)
{
  return stat(path, st);
}

static bool host_fb_tty_active(void)
{
  return false;
}

static void host_fb_tty_on_fork_child(void) {}
static void host_fb_tty_blink_tick(void) {}

static bool host_is_builtin(const char *name)
{
  (void)name;
  return false;
}

static int host_run_builtin(int argc, char *const argv[])
{
  (void)argc;
  (void)argv;
  return -1;
}

const vega_host_ops_t vega_cli_host = {
    .puts                 = host_puts,
    .stdout_bytes         = host_stdout_bytes,
    .close                = host_close,
    .read                 = host_read,
    .stat                 = host_stat,
    .fb_tty_active        = host_fb_tty_active,
    .fb_tty_on_fork_child = host_fb_tty_on_fork_child,
    .fb_tty_blink_tick    = host_fb_tty_blink_tick,
    .is_builtin           = host_is_builtin,
    .run_builtin          = host_run_builtin,
};
