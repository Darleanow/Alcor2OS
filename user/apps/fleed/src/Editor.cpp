/**
 * @file fleed/src/Editor.cpp
 * @brief Editor event loop: loads the file, classifies UI events into
 *        commands, applies them to the buffer, then asks the view to repaint.
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
    spz_event_t ev {};
    if(m_ui.pollEvent(ev) < 0)
      return 1;
    KeyAction act = classify(ev);

    /* Cases that touch the view themselves (redraw / insertChar) continue;
     * cursor-only moves fall through to the single refreshCursor below. */
    switch(act.cmd) {
    case Command::Quit:
      return 0;
    case Command::None:
      continue;

    case Command::Insert:
      insertChar(act.data);
      continue;

    case Command::Backspace:
      if(m_buffer.popBack()) {
        m_ui.redraw(m_buffer);
        continue;
      }
      break;

    case Command::Enter:
      m_buffer.newLine();
      m_ui.redraw(m_buffer);
      continue;

    case Command::Save:
      handleSave();
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
      m_buffer.cursorMoveLineStart();
      break;
    case Command::End:
      m_buffer.cursorMoveLineEnd();
      break;
    }

    m_ui.refreshCursor(m_buffer);
  }
}

void Editor::insertChar(wchar_t ch)
{
  std::array<char, MB_LEN_MAX> mb {};
  std::mbstate_t               st {};
  std::size_t                  n = std::wcrtomb(mb.data(), ch, &st);
  /* Drop the keystroke on encoding failure rather than insert garbage. */
  if(n == static_cast<std::size_t>(-1))
    return;
  m_buffer.append(mb.data(), n);
  m_ui.redrawLine(m_buffer, m_buffer.cursor().y);
  m_ui.refreshCursor(m_buffer);
}

void Editor::handleSave()
{
  m_ui.setStatus(m_buffer.save(m_path) ? "saved" : "save failed");
}

} /* namespace fleed */
