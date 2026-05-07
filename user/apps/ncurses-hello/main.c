/**
 * ncurses smoke test — Alcor2 framebuffer + musl.
 * Explicit move/addstr exercises CSI cursor positioning in the kernel console.
 * UTF-8: U+0000–U+00FF maps to the Latin-1 font; other code points show '?'.
 */
#include <curses.h>

int main(void)
{
  if(!initscr())
    return 1;

  raw();
  keypad(stdscr, TRUE);
  noecho();
  curs_set(1);

  clear();
  refresh();

  move(2, 2);
  addstr("ncurses on Alcor2 (CUP + CUD/CUU fixed in kernel)");
  refresh();

  move(4, 2);
  addstr("Latin-1 UTF-8: flambé  (U+00E9 two-byte UTF-8)");
  refresh();

  move(6, 2);
  addstr("Outside Latin-1 block -> ?  (e.g. CJK / emoji)");
  refresh();

  move(10, 2);
  addstr("Press any key - cursor should be visible above.");
  refresh();

  (void)getch();

  endwin();
  return 0;
}
