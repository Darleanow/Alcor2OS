/**
 * @file echo.c
 * @brief Print arguments to standard output, separated by spaces.
 */

#include <grendizer.h>

#include <stdio.h>

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {.program = "echo", .usage = "[string ...]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  for(int i = 0; i < rest.argc; i++) {
    if(i > 0)
      printf(" ");
    printf("%s", rest.argv[i]);
  }
  printf("\n");
  return 0;
}
