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
    return {Command::None, 0};
  }

  if(kind == OK && (std::iswprint(ch) || ch == L'\n'))
    return {Command::Insert, static_cast<wchar_t>(ch)};

  return {Command::None, 0};
}

} /* namespace fleed */
