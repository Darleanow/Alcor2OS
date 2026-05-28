/**
 * @file platform/vconf.c
 * @brief Source @c /home/.vconf (or any path) as a vega script on startup.
 *
 * Equivalent of @c .bashrc — runs any vega language feature (let, if, fn,
 * pipes, builtins like @c kbd). Stdout is redirected to @c /dev/null for the
 * duration so the script's commands don't print to the user's console.
 */

#include <fcntl.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <unistd.h>
#include <vega/vega.h>

#define VCONF_INITIAL_CAP 4096

/** @brief Read the whole content of @p fd into a freshly malloc'd buffer.
 *
 * @param fd      Open read-only file descriptor.
 * @param out_len Receives the byte length (excluding the trailing NUL).
 * @return Heap-allocated NUL-terminated buffer (caller frees), or @c NULL on
 *         OOM or zero-byte file.
 */
static char *slurp_fd(int fd, size_t *out_len)
{
  size_t cap = VCONF_INITIAL_CAP;
  size_t len = 0;
  char  *src = (char *)malloc(cap);
  if(!src)
    return NULL;
  for(;;) {
    if(len + 1 >= cap) {
      cap *= 2;
      char *nb = (char *)realloc(src, cap);
      if(!nb) {
        free(src);
        return NULL;
      }
      src = nb;
    }
    ssize_t n = read(fd, src + len, cap - len - 1);
    if(n <= 0)
      break;
    len += (size_t)n;
  }
  src[len] = '\0';
  *out_len = len;
  return src;
}

void sh_source_vconf(const char *path)
{
  int cfd = open(path, O_RDONLY);
  if(cfd < 0)
    return;

  size_t len;
  char  *src = slurp_fd(cfd, &len);
  close(cfd);
  if(!src)
    return;

  /* Silence stdout so .vconf commands don't print to the console. */
  int saved_out = -1;
  int devnull   = open("/dev/null", O_WRONLY);
  if(devnull >= 0) {
    saved_out = dup(STDOUT_FILENO);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);
  }
  vega_run(src);
  if(saved_out >= 0) {
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
  }
  free(src);
}
