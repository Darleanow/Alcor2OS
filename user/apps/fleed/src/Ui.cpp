/**
 * @file fleed/src/Ui.cpp
 */

#include <fleed/Ui.hpp>

#include <cstdio>
#include <cstdlib>

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

void Ui::redraw(const std::string &text)
{
  wclear(m_editor->body);
  waddstr(m_editor->body, text.c_str());
  wrefresh(m_editor->body);
}

void Ui::putChar(wchar_t ch)
{
  waddch(m_editor->body, static_cast<chtype>(ch));
  wrefresh(m_editor->body);
}

void Ui::setStatus(const char *text)
{
  spz_statusbar("fleed", text, kHints);
}

} /* namespace fleed */
