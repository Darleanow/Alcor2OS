/**
 * mkdir - Create directory
 *
 * Usage: mkdir <dir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
  if(argc < 2) {
    printf("mkdir: missing argument\n");
    return 1;
  }

  const char *path = argv[1];

  if(mkdir(path, 0755) < 0) {
    printf("mkdir: cannot create '%s'\n", path);
    return 1;
  }

  return 0;
}
