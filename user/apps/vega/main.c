/**
 * @file apps/vega/main.c
 * @brief Standalone vega interpreter CLI.
 *
 *   vega script.veg     run a script file
 *   vega -c "code"      evaluate a string
 *   vega                read from stdin until EOF, then evaluate
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vega/vega.h>

static int run_string(const char *src)
{
  return vega_run(src);
}

static char *slurp(int fd, size_t *out_len)
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
  if(out_len)
    *out_len = len;
  return buf;
}

static int run_file(const char *path)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "vega: cannot open %s\n", path);
    return 1;
  }
  char *src = slurp(fd, NULL);
  close(fd);
  if(!src) {
    fprintf(stderr, "vega: read failed for %s\n", path);
    return 1;
  }
  int rc = vega_run(src);
  free(src);
  return rc;
}

static void usage(void)
{
  fprintf(
      stderr, "usage: vega [-c CODE | SCRIPT]\n"
              "  -c CODE    evaluate CODE and exit\n"
              "  SCRIPT     run script file\n"
              "  (no args)  read program from stdin\n"
  );
}

int main(int argc, char *argv[])
{
  if(argc == 1) {
    char *src = slurp(STDIN_FILENO, NULL);
    if(!src) {
      fprintf(stderr, "vega: read failed\n");
      return 1;
    }
    int rc = run_string(src);
    free(src);
    return rc;
  }
  if(argc == 2) {
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      usage();
      return 0;
    }
    if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
      printf("vega %s\n", VEGA_VERSION);
      return 0;
    }
    return run_file(argv[1]);
  }
  if(argc == 3 && strcmp(argv[1], "-c") == 0) {
    return run_string(argv[2]);
  }
  usage();
  return 2;
}
