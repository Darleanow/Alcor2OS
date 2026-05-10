/**
 * ncurses smoke test — Alcor2 framebuffer + musl.
 * Explicit move/addstr exercises CSI cursor positioning in the kernel console.
 * UTF-8: U+0000–U+00FF maps to the Latin-1 font; other code points show '?'.
 */
#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
  const char *term = getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  /* Unbuffer before passing to newterm so pipe output reaches the relay
   * immediately. */
  (void)setvbuf(stdout, NULL, _IONBF, 0);

  SCREEN *scr = newterm(term, stdout, stdin);
  if(!scr) {
    fputs(
        "ncurses-hello: newterm() failed — check /usr/share/terminfo "
        "(host: make ncurses; guest: . /etc/profile)\n",
        stderr
    );
    return 1;
  }
  set_term(scr);

  /* Disable typeahead: avoids read() on stdin during refresh when relayed via
   * pipe. */
  typeahead(-1);

  raw();
  keypad(stdscr, TRUE);
  noecho();
  curs_set(1);

  clear();

  move(2, 2);
  addstr("ncurses on Alcor2 (CUP + CUD/CUU fixed in kernel)");

  move(4, 2);
  addstr("Latin-1 UTF-8: flamb\xc3\xa9  (U+00E9 two-byte UTF-8)");

  move(6, 2);
  addstr("Outside Latin-1 block -> ?  (e.g. CJK / emoji)");

  move(10, 2);
  addstr("Appuyez sur une touche pour quitter (curseur au-dessus).");

  refresh();

  (void)getch();

  endwin();
  delscreen(scr);
  return 0;
}
