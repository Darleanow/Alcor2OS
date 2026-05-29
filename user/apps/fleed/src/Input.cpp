/**
 * @file fleed/src/Input.cpp
 */

#include <fleed/Input.hpp>

#include <cwctype>

namespace fleed {

KeyAction classify(int kind, wint_t ch)
{
  if(kind == KEY_CODE_YES) {
    if(ch == static_cast<wint_t>(KEY_F(1)))
      return {Command::Quit, 0};
    if(ch == static_cast<wint_t>(KEY_F(2)))
      return {Command::Save, 0};
    if(ch == static_cast<wint_t>(KEY_BACKSPACE))
      return {Command::Backspace, 0};
    if(ch == static_cast<wint_t>(KEY_DOWN))
      return {Command::ArrowDown, 0};
    if(ch == static_cast<wint_t>(KEY_UP))
      return {Command::ArrowUp, 0};
    if(ch == static_cast<wint_t>(KEY_LEFT))
      return {Command::ArrowLeft, 0};
    if(ch == static_cast<wint_t>(KEY_RIGHT))
      return {Command::ArrowRight, 0};
    if(ch == static_cast<wint_t>(KEY_HOME))
      return {Command::Home, 0};
    if(ch == static_cast<wint_t>(KEY_END))
      return {Command::End, 0};
    if(ch == static_cast<wint_t>(KEY_ENTER))
      return {Command::Enter, 0};
    return {Command::None, 0};
  }

  if(kind == OK) {
    if(ch == L'\n')
      return {Command::Enter, 0};
    if(std::iswprint(ch))
      return {Command::Insert, static_cast<wchar_t>(ch)};
  };

  return {Command::None, 0};
}

} /* namespace fleed */
