/**
 * @file user/bin/rmdir.c
 * @brief Remove empty directories.
 */

#include <grendizer.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int remove_dir(const char *path)
{
  if(rmdir(path) < 0) {
    (void)fprintf(stderr, "rmdir: failed to remove '%s'\n", path);
    return -1;
  }
  return 0;
}

static int remove_with_parents(const char *path, int parents)
{
  char buf[4096];
  int  len = (int)strlen(path);

  if(len == 0 || len >= (int)sizeof(buf)) {
    (void)fprintf(stderr, "rmdir: invalid path\n");
    return -1;
  }

  (void)memcpy(buf, path, (size_t)(len + 1));

  /* Strip trailing slashes, except for root. */
  while(len > 1 && buf[len - 1] == '/')
    buf[--len] = '\0';

  if(remove_dir(buf) < 0)
    return -1;

  if(!parents)
    return 0;

  while(len > 1) {
    int i = len - 1;
    while(i > 0 && buf[i] != '/')
      i--;

    if(i <= 0)
      break;

    buf[i] = '\0';
    len    = i;

    if(remove_dir(buf) < 0)
      return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  int show_parents = 0;

  gr_opt opts[] = {
      GR_FLAG(
          'p',
          "parents",
          &show_parents,
          "remove directory and its ancestors (e.g. rmdir -p a/b/c removes "
          "a/b/c, a/b, then a)"
      ),
      GR_END
  };

  gr_spec spec = {
      .program = "rmdir", .usage = "[options] <dir> [...]", .options = opts
  };

  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    (void)fprintf(stderr, "rmdir: missing operand\n");
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    if(remove_with_parents(rest.argv[i], show_parents) < 0)
      exit_code = 1;
  }
  return exit_code;
}
