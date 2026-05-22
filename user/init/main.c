/**
 * @file main.c
 * @brief Userland init process — first Ring 3 program launched by the kernel.
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
