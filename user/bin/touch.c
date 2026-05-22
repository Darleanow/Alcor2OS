/**
 * @file touch.c
 * @brief Create empty files or update their timestamps.
 */

#include <grendizer.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec = {.program = "touch", .usage = "<file> [...]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    fprintf(stderr, "touch: missing operand\n");
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    int fd = open(rest.argv[i], O_CREAT | O_WRONLY, 0644);
    if(fd < 0) {
      fprintf(stderr, "touch: cannot create '%s'\n", rest.argv[i]);
      exit_code = 1;
      continue;
    }
    close(fd);
  }
  return exit_code;
}
