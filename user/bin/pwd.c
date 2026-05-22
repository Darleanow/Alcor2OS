/**
 * @file pwd.c
 * @brief Print the current working directory.
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  char buf[256];
  if(getcwd(buf, sizeof(buf)) != NULL) {
    printf("%s\n", buf);
  }
  return 0;
}
