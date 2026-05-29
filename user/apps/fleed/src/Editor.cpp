/**
 * @file fleed/src/Editor.cpp
 */

#include <fleed/Editor.hpp>
#include <fleed/Input.hpp>

#include <array>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <iostream>

namespace fleed {

Editor::Editor(std::filesystem::path path) : m_path(std::move(path)) {}

int Editor::run()
{
  if(!m_buffer.load(m_path)) {
    std::cerr << "fleed: cannot read file\n";
    return 1;
  }

  if(!m_ui.init(std::string("Fleed ") + m_path.filename().c_str())) {
    std::cerr << "fleed: failed to initialise UI\n";
    return 1;
  }
  m_ui.redraw(m_buffer);

  for(;;) {
    wint_t ch   = 0;
    int    kind = m_ui.readKey(ch);
    auto   act  = classify(kind, ch);

    switch(act.cmd) {
    case Command::Quit:
      return 0;
    case Command::Save:
      handleSave();
      break;
    case Command::Backspace:
      if(m_buffer.popBack())
        m_ui.redraw(m_buffer);
      break;
    case Command::Insert:
      insertChar(act.data);
      break;
    case Command::ArrowUp:
      m_buffer.cursorMoveUp();
      break;
    case Command::ArrowDown:
      m_buffer.cursorMoveDown();
      break;
    case Command::ArrowLeft:
      m_buffer.cursorMoveLeft();
      break;
    case Command::ArrowRight:
      m_buffer.cursorMoveRight();
      break;
    case Command::Home:
      m_buffer.setCursorPos(0, m_buffer.cursor().y);
      break;
    case Command::End:
      m_buffer.setCursorPos(
          m_buffer.line(m_buffer.cursor().y).size(), m_buffer.cursor().y
      );
      break;
    case Command::Enter:
      m_buffer.newLine();
      m_ui.redraw(m_buffer);
      break;
    case Command::None:
      break;
    }
    m_ui.redrawCursor(m_buffer);
  }
}

void Editor::insertChar(wchar_t ch)
{
  std::array<char, MB_LEN_MAX> mb {};
  std::mbstate_t               st {};
  std::size_t                  n = std::wcrtomb(mb.data(), ch, &st);
  if(n == static_cast<std::size_t>(-1))
    return;
  m_buffer.append(mb.data(), n);
  m_ui.redrawLine(m_buffer, m_buffer.cursor().y);
}

void Editor::handleSave()
{
  m_ui.setStatus(m_buffer.save(m_path) ? "saved" : "save failed");
}

} /* namespace fleed */
