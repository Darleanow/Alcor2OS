/**
 * @file fleed/src/Ui.cpp
 */

#include <fleed/Ui.hpp>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace fleed {

namespace {
constexpr const char *kHints = "F1 quit F2 save";
}

Ui::Ui() = default;

Ui::~Ui()
{
  shutdown();
}

bool Ui::init(const std::string &header)
{
  const char *term = std::getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  (void)std::setvbuf(stdout, nullptr, _IONBF, 0);

  m_scr = newterm(term, stdout, stdin);
  if(!m_scr)
    return false;
  set_term(m_scr);
  typeahead(-1);
  raw();
  keypad(stdscr, TRUE);
  noecho();
  curs_set(1);
  spz_init();

  int rows = 0, cols = 0;
  getmaxyx(stdscr, rows, cols);

  m_editor =
      spz_panel_new(0, 0, rows - 1, cols, header.c_str(), SPZ_BORDER_SINGLE);
  if(!m_editor) {
    shutdown();
    return false;
  }
  keypad(m_editor->body, TRUE);
  /* Force DECCKM (application cursor keys mode): the kernel fb_console
   * emits SS3 (\EOA) for arrow keys when this is set and CSI (\E[A) when
   * cleared. ncurses' keypad() sends "smkx" via terminfo to flip it on, but
   * relying on terminfo is brittle here — write it explicitly so arrows
   * reach wget_wch as KEY_UP/DOWN/LEFT/RIGHT instead of raw bytes. */
  (void)write(STDOUT_FILENO, "\033[?1h", 5);
  spz_statusbar("fleed", nullptr, kHints);
  spz_panel_refresh(m_editor);
  return true;
}

void Ui::shutdown()
{
  if(m_editor) {
    spz_panel_del(m_editor);
    m_editor = nullptr;
  }
  if(m_scr) {
    endwin();
    delscreen(m_scr);
    m_scr = nullptr;
  }
}

int Ui::readKey(wint_t &out)
{
  return wget_wch(m_editor->body, &out);
}

void Ui::redraw(const Buffer &buffer)
{
  wclear(m_editor->body);

  for(size_t i = 0; i < buffer.lineCount(); i++) {
    mvwaddstr(m_editor->body, i, 0, buffer.line(i).c_str());
  }
  wmove(
      m_editor->body, static_cast<int>(buffer.cursor().y),
      static_cast<int>(buffer.cursor().x)
  );

  spz_panel_refresh(m_editor);
}

void Ui::putChar(wchar_t ch)
{
  waddch(m_editor->body, static_cast<chtype>(ch));
  spz_panel_refresh(m_editor);
}

void Ui::setStatus(const char *text)
{
  spz_statusbar("fleed", text, kHints);
}

void Ui::redrawCursor(const Buffer &buffer)
{
  wmove(
      m_editor->body, static_cast<int>(buffer.cursor().y),
      static_cast<int>(buffer.cursor().x)
  );
  spz_panel_refresh(m_editor);
}

} /* namespace fleed */
