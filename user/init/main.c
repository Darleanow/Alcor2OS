/**
 * @file user/init/main.c
 * @brief Minimal init process (demo message); the kernel starts the shell separately.
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  printf("Hello from userspace!\n");
  printf("Alcor2 init process running in Ring 3.\n");

  return 0;
}
