/**
 * kbd - Set the PS/2 keyboard layout via ioctl.
 *
 * Usage: kbd us|fr
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include <alcor2/kbd.h>

int main(int argc, char *argv[])
{
  if(argc != 2) {
    fprintf(stderr, "usage: kbd us|fr\n");
    return 1;
  }

  uint32_t layout;
  if(strcmp(argv[1], "us") == 0)
    layout = KBD_LAYOUT_US;
  else if(strcmp(argv[1], "fr") == 0)
    layout = KBD_LAYOUT_FR;
  else {
    fprintf(stderr, "kbd: unknown layout '%s' (expected us|fr)\n", argv[1]);
    return 1;
  }

  if(ioctl(0, ALCOR2_IOC_KBD_SET_LAYOUT, &layout) < 0) {
    perror("kbd: ioctl");
    return 1;
  }
  printf("keyboard: layout %s\n", argv[1]);
  return 0;
}
