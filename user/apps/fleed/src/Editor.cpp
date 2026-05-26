/**
 * @file fleed/src/Editor.cpp
 */

#include <fleed/Editor.hpp>
#include <fleed/Input.hpp>

#include <array>
#include <climits>
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
  m_ui.redraw(m_buffer.text());

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
        m_ui.redraw(m_buffer.text());
      break;
    case Command::Insert:
      insertChar(act.data);
      break;
    case Command::None:
      break;
    }
  }
}

void Editor::insertChar(wchar_t ch)
{
  std::array<char, MB_LEN_MAX> mb{};
  std::mbstate_t               st{};
  std::size_t                  n = std::wcrtomb(mb.data(), ch, &st);
  if(n == static_cast<std::size_t>(-1))
    return;
  m_buffer.append(mb.data(), n);
  m_ui.putChar(ch);
}

void Editor::handleSave()
{
  m_ui.setStatus(m_buffer.save(m_path) ? "saved" : "save failed");
}

} /* namespace fleed */
