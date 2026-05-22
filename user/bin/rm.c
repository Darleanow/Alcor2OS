/**
 * @file rm.c
 * @brief Remove files or directories.
 */

#include <grendizer.h>

#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {.program = "rm", .usage = "<file> [...]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    fprintf(stderr, "rm: missing operand\n");
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    if(unlink(rest.argv[i]) < 0) {
      fprintf(stderr, "rm: cannot remove '%s'\n", rest.argv[i]);
      exit_code = 1;
    }
  }
  return exit_code;
}
