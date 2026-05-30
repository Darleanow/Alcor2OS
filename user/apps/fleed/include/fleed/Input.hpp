/**
 * @file fleed/Input.hpp
 * @brief Translate a decoded spazer event into a high-level editor command.
 */
#pragma once

#include <cstdint>
#include <spazer/spazer.h>

namespace fleed {

/** @brief Editor-level operations that an event can request. */
enum class Command : std::uint8_t
{
  None,       /**< Unhandled event; the caller should ignore it. */
  Quit,       /**< Leave the editor without saving. */
  Save,       /**< Persist the buffer to its file. */
  Backspace,  /**< Delete the byte before the cursor or fuse two lines. */
  Insert,     /**< Insert @c KeyAction::data into the buffer at the cursor. */
  ArrowUp,    /**< Move the cursor one row up. */
  ArrowDown,  /**< Move the cursor one row down. */
  ArrowLeft,  /**< Move the cursor one column left. */
  ArrowRight, /**< Move the cursor one column right. */
  Home,       /**< Move the cursor to the start of the current line. */
  End,        /**< Move the cursor to the end of the current line. */
  Enter       /**< Split the current line at the cursor. */
};

/**
 * @brief Result of @ref classify: a command and, for @ref Command::Insert,
 *        the codepoint to insert.
 */
struct KeyAction
{
  Command cmd;  /**< Command requested by the event. */
  wchar_t data; /**< Codepoint to insert; only valid when @c cmd is Insert. */
};

/**
 * @brief Classify one decoded spazer event.
 *
 * @param ev  Event from @c spz_poll.
 * @return    Command the event maps to (@ref Command::None if unhandled).
 */
KeyAction classify(const spz_event_t &ev);

} /* namespace fleed */
