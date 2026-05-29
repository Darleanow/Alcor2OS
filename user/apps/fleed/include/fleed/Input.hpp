/**
 * @file fleed/Input.hpp
 * @brief Translate a raw ncurses keystroke into a high-level editor command.
 */
#pragma once

#include <cstdint>
#include <curses.h>

namespace fleed {

/** @brief Editor-level operations that a keystroke can request. */
enum class Command : std::uint8_t
{
  None,      /**< Unhandled keystroke; the caller should ignore it. */
  Quit,      /**< Leave the editor without saving. */
  Save,      /**< Persist the buffer to its file. */
  Backspace, /**< Remove the last byte of the buffer. */
  Insert,    /**< Insert @c KeyAction::data into the buffer at the cursor. */
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  Home,
  End
};

/** @brief Result of @ref classify: a command and, for @ref Command::Insert,
 *         the codepoint to insert. */
struct KeyAction
{
  Command cmd;  /**< Command requested by the keystroke. */
  wchar_t data; /**< Codepoint to insert; only valid when @c cmd is Insert. */
};

/**
 * @brief Classify one keystroke read from @ref Ui::readKey.
 * @param kind The @c kind value returned by @c wget_wch
 *             (@c OK, @c KEY_CODE_YES or @c ERR).
 * @param ch   The keystroke value: a codepoint when @p kind is @c OK,
 *             a @c KEY_* constant when @p kind is @c KEY_CODE_YES.
 * @return The command the keystroke maps to (@ref Command::None if unhandled).
 */
KeyAction classify(int kind, wint_t ch);

} /* namespace fleed */
