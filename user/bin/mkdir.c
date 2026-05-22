/**
 * @file mkdir.c
 * @brief Create one or more directories.
 */

#include <grendizer.h>

#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {.program = "mkdir", .usage = "<dir> [...]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    fprintf(stderr, "mkdir: missing operand\n");
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    if(mkdir(rest.argv[i], 0755) < 0) {
      fprintf(stderr, "mkdir: cannot create '%s'\n", rest.argv[i]);
      exit_code = 1;
    }
  }
  return exit_code;
}
