/**
 * @file fleed/src/Input.cpp
 * @brief Map a decoded spazer key event to an editor-level @ref Command.
 *
 * Pure translation, no state. Anything not in the table degrades to
 * @c Command::None so the event loop can ignore it cleanly.
 */

#include <fleed/Input.hpp>

#include <cwctype>

namespace fleed {

KeyAction classify(const spz_event_t &ev)
{
  if(ev.kind != SPZ_EV_KEY)
    return {Command::None, 0};

  switch(ev.key.kind) {
  case SPZ_KEY_F1:
    return {Command::Quit, 0};
  case SPZ_KEY_F2:
    return {Command::Save, 0};
  case SPZ_KEY_BACKSPACE:
    return {Command::Backspace, 0};
  case SPZ_KEY_UP:
    return {Command::ArrowUp, 0};
  case SPZ_KEY_DOWN:
    return {Command::ArrowDown, 0};
  case SPZ_KEY_LEFT:
    return {Command::ArrowLeft, 0};
  case SPZ_KEY_RIGHT:
    return {Command::ArrowRight, 0};
  case SPZ_KEY_HOME:
    return {Command::Home, 0};
  case SPZ_KEY_END:
    return {Command::End, 0};
  case SPZ_KEY_ENTER:
    return {Command::Enter, 0};
  case SPZ_KEY_CHAR:
    if(std::iswprint(ev.key.ch))
      return {Command::Insert, ev.key.ch};
    return {Command::None, 0};
  default:
    return {Command::None, 0};
  }
}

} /* namespace fleed */
