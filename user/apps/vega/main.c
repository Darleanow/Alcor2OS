/**
 * @file apps/vega/main.c
 * @brief Standalone vega interpreter CLI.
 *
 *   vega SCRIPT          run a script file
 *   vega -c CODE         evaluate CODE and exit
 *   vega                 read program from stdin until EOF
 */

#include <fcntl.h>
#include <grendizer.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vega/vega.h>

static char *slurp(int fd)
{
  size_t cap = 4096;
  size_t len = 0;
  char  *buf = (char *)malloc(cap);
  if(!buf)
    return NULL;
  for(;;) {
    if(len + 1 >= cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if(!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
    ssize_t n = read(fd, buf + len, cap - len - 1);
    if(n < 0) {
      free(buf);
      return NULL;
    }
    if(n == 0)
      break;
    len += (size_t)n;
  }
  buf[len] = '\0';
  return buf;
}

static int run_file(const char *path)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "vega: cannot open %s\n", path);
    return 1;
  }
  char *src = slurp(fd);
  close(fd);
  if(!src) {
    fprintf(stderr, "vega: read failed for %s\n", path);
    return 1;
  }
  int rc = vega_run(src);
  free(src);
  return rc;
}

static int run_stdin(void)
{
  char *src = slurp(STDIN_FILENO);
  if(!src) {
    fprintf(stderr, "vega: read failed\n");
    return 1;
  }
  int rc = vega_run(src);
  free(src);
  return rc;
}

int main(int argc, char *argv[])
{
  const char *code    = NULL;
  int         version = 0;

  gr_opt      opts[] = {
      GR_STR('c', "code", &code, "CODE", "evaluate CODE and exit"),
      GR_FLAG('v', "version", &version, "print version and exit"),
      GR_END,
  };
  gr_spec spec = {
      .program = "vega",
      .usage   = "vega [-c CODE | SCRIPT]",
      .options = opts,
      .epilog  = "  (no args)  read program from stdin",
  };
  gr_rest rest;
  char    errbuf[128];

  int     rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof errbuf);
  if(rc == GR_HELP)
    return 0;
  if(rc == GR_ERR) {
    fprintf(stderr, "vega: %s\n", errbuf);
    return 2;
  }

  if(version) {
    printf("vega %s\n", VEGA_VERSION);
    return 0;
  }
  if(code)
    return vega_run(code);
  if(rest.argc >= 1)
    return run_file(rest.argv[0]);
  return run_stdin();
}
