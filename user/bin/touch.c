/**
 * touch - Create empty file
 *
 * Usage: touch <file>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  if(argc < 2) {
    printf("touch: missing argument\n");
    return 1;
  }

  const char *path = argv[1];

  int         fd = open(path, O_CREAT | O_WRONLY, 0644);
  if(fd < 0) {
    printf("touch: cannot create '%s'\n", path);
    return 1;
  }

  close(fd);
  return 0;
}
