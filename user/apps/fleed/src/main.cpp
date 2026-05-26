/**
 * @file user/apps/fleed/src/main.cpp
 * @brief Fleed — terminal text editor for Alcor2.
 *
 * Entry point: parse arguments with Grendizer, then open the editor window
 * using Spazer for theming and layout.
 */

#include <curses.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <grendizer.h>
#include <spazer/spazer.h>
#include <stdlib.h>
#include <string>
#include <wchar.h>

int main(int argc, char **argv)
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"fleed", "<file>", opts, nullptr};
  gr_rest rest;
  char    errbuf[128];

  int     rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof errbuf);
  if(rc == GR_HELP)
    return 0;
  if(rc != GR_OK) {
    (void)fputs(errbuf, stderr);
    (void)fputc('\n', stderr);
    return 1;
  }
  if(rest.argc < 1) {
    gr_usage(&spec, stderr);
    return 1;
  }

  const char *path = rest.argv[0];

  const char *term = getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  (void)setvbuf(stdout, nullptr, _IONBF, 0);

  SCREEN *scr = newterm(term, stdout, stdin);
  if(!scr) {
    (void)fputs("fleed: newterm() failed\n", stderr);
    return 1;
  }
  set_term(scr);
  typeahead(-1);

  raw();
  keypad(stdscr, TRUE);
  noecho();
  curs_set(1);

  spz_init();

  std::fstream f;

  int          rows, cols;
  getmaxyx(stdscr, rows, cols);

  std::filesystem::path p(path);
  f.open(p, std::ios::in | std::ios::out);
  if(!f.is_open()) {
    (void)fputs("fleed: failed to open file for writing\n", stderr);
    return 1;
  }

  std::stringstream ss;
  ss << f.rdbuf();
  f.close();
  std::string  buf {ss.str()};

  std::string  header = std::string("Fleed ") + p.filename().c_str();
  spz_panel_t *editor =
      spz_panel_new(0, 0, rows - 1, cols, header.c_str(), SPZ_BORDER_SINGLE);
  if(!editor) {
    endwin();
    delscreen(scr);
    (void)fputs("fleed: failed to create editor panel\n", stderr);
    return 1;
  }

  keypad(editor->body, TRUE);
  spz_statusbar("fleed", nullptr, "F1 quit F2 save");
  spz_panel_refresh(editor);
  waddstr(editor->body, buf.c_str());
  wrefresh(editor->body);

  for(;;) {
    wint_t ch;
    int    kind = wget_wch(editor->body, &ch);

    if(kind == KEY_CODE_YES && ch == (wint_t)KEY_F(1))
      break;

    if(kind == KEY_CODE_YES && ch == (wint_t)KEY_F(2)) {
      FILE *out = fopen(path, "w");
      if(out) {
        (void)fwrite(buf.c_str(), 1, buf.size(), out);
        (void)fclose(out);
      }
      continue;
    }

    if(kind == KEY_CODE_YES && ch == (wint_t)KEY_BACKSPACE) {
      if(!buf.empty()) {
        buf.pop_back();
        wclear(editor->body);
        waddstr(editor->body, buf.c_str());
        wrefresh(editor->body);
      }
      continue;
    }

    if(kind == OK && (iswprint((wint_t)ch) || ch == L'\n')) {
      char mb[MB_CUR_MAX + 1];
      int  n = wctomb(mb, (wchar_t)ch);
      if(n > 0) {
        mb[n] = '\0';
        buf.append(mb, (size_t)n);
        waddch(editor->body, ch);
        wrefresh(editor->body);
      }
    }
  }

  spz_panel_del(editor);
  endwin();
  delscreen(scr);
  return 0;
}
