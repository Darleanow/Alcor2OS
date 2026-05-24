/**
 * @file mouse.c
 * @brief Read events from /dev/mouse and optionally toggle relative mode.
 *
 * Usage:
 *   mouse           — stream events to stdout
 *   mouse rel       — enable relative mode, then stream
 *   mouse norel     — disable relative mode and exit
 */

#include <grendizer.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <alcor2/mouse.h>

static int set_relative(int fd, uint32_t v)
{
  if(ioctl(fd, ALCOR2_IOC_MOUSE_SET_RELATIVE, &v) < 0) {
    perror("mouse: ioctl");
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec = {.program = "mouse", .usage = "[rel|norel]", .options = opts};
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  int fd = open("/dev/mouse", O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "mouse: open /dev/mouse: %s\n", strerror(errno));
    return 1;
  }

  if(rest.argc >= 1 && strcmp(rest.argv[0], "norel") == 0) {
    int rv = set_relative(fd, 0);
    if(rv == 0)
      printf("relative mode OFF\n");
    close(fd);
    return rv;
  }

  int was_rel = 0;
  if(rest.argc >= 1 && strcmp(rest.argv[0], "rel") == 0) {
    if(set_relative(fd, 1) != 0) {
      close(fd);
      return 1;
    }
    was_rel = 1;
    printf("relative mode ON — cursor pinned. Left-click to exit.\n");
  } else {
    printf("mouse events on /dev/mouse — left-click to exit\n");
  }

  alcor2_mouse_event_t ev;
  u8                   prev_btn = 0;
  for(;;) {
    ssize_t n = read(fd, &ev, sizeof(ev));
    if(n < 0) {
      perror("mouse: read");
      break;
    }
    if(n == 0)
      break;
    printf(
        "dx=%-5d dy=%-5d wheel=%-3d btn=0x%02x\n", (int)ev.dx, (int)ev.dy,
        (int)ev.dwheel, ev.buttons
    );
    /* Edge-trigger on left press. */
    if((ev.buttons & ALCOR2_MOUSE_BTN_LEFT) &&
       !(prev_btn & ALCOR2_MOUSE_BTN_LEFT))
      break;
    prev_btn = ev.buttons;
  }

  if(was_rel)
    set_relative(fd, 0);
  close(fd);
  return 0;
}
