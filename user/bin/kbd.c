/**
 * @file kbd.c
 * @brief Set the active keyboard layout (us|fr) via ioctl.
 */

#include <grendizer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include <alcor2/kbd.h>

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {.program = "kbd", .usage = "us|fr", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc != 1) {
    (void)fprintf(stderr, "usage: kbd us|fr\n");
    return 1;
  }

  uint32_t layout;
  if(strcmp(rest.argv[0], "us") == 0)
    layout = KBD_LAYOUT_US;
  else if(strcmp(rest.argv[0], "fr") == 0)
    layout = KBD_LAYOUT_FR;
  else {
    (void)fprintf(
        stderr, "kbd: unknown layout '%s' (expected us|fr)\n", rest.argv[0]
    );
    return 1;
  }

  if(ioctl(0, ALCOR2_IOC_KBD_SET_LAYOUT, &layout) < 0) {
    perror("kbd: ioctl");
    return 1;
  }
  (void)printf("keyboard: layout %s\n", rest.argv[0]);
  return 0;
}
