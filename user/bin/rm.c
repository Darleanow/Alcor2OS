/**
 * rm - Remove file
 *
 * Usage: rm <file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  if(argc < 2) {
    printf("rm: missing argument\n");
    return 1;
  }

  const char *path = argv[1];

  if(unlink(path) < 0) {
    printf("rm: cannot remove '%s'\n", path);
    return 1;
  }

  return 0;
}
