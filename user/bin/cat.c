/**
 * cat - Display file contents
 *
 * Usage: cat <file>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int copy_to_stdout(int fd)
{
  char    buf[512];
  ssize_t n;
  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(STDOUT_FILENO, buf, (size_t)n);
  return n < 0 ? 1 : 0;
}

int main(int argc, char *argv[])
{
  if(argc < 2)
    return copy_to_stdout(STDIN_FILENO);

  const char *path = argv[1];
  int         fd   = open(path, O_RDONLY);
  if(fd < 0) {
    printf("cat: cannot open '%s'\n", path);
    return 1;
  }
  int rc = copy_to_stdout(fd);
  close(fd);
  return rc;
}
