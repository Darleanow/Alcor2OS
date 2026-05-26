/**
 * @file cat.c
 * @brief Concatenate files and print to standard output.
 */

#include <grendizer.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int copy_to_stdout(int fd)
{
  char    buf[512];
  ssize_t n;
  while((n = read(fd, buf, sizeof(buf))) > 0)
    (void)write(STDOUT_FILENO, buf, (size_t)n);
  return n < 0 ? 1 : 0;
}

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {.program = "cat", .usage = "[file ...]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0)
    return copy_to_stdout(STDIN_FILENO);

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    int fd = open(rest.argv[i], O_RDONLY);
    if(fd < 0) {
      (void)fprintf(stderr, "cat: cannot open '%s'\n", rest.argv[i]);
      exit_code = 1;
      continue;
    }
    if(copy_to_stdout(fd))
      exit_code = 1;
    close(fd);
  }
  return exit_code;
}
