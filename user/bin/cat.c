/**
 * cat - Display file contents
 *
 * Usage: cat <file>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  if(argc < 2) {
    printf("cat: missing file argument\n");
    return 1;
  }

  const char *path = argv[1];

  int         fd = open(path, O_RDONLY);
  if(fd < 0) {
    printf("cat: cannot open '%s'\n", path);
    return 1;
  }

  char    buf[512];
  ssize_t n;
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    write(STDOUT_FILENO, buf, (size_t)n);
  }

  close(fd);
  return 0;
}
