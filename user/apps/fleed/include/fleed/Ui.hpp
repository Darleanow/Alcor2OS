/**
 * @file fleed/Ui.hpp
 * @brief ncurses + spazer wrapper for fleed's editor view.
 *
 * Owns the SCREEN / panel and provides the only API the rest of the editor
 * uses to read keystrokes and update the display, so the input layer never
 * touches ncurses directly.
 */
#pragma once

#include <curses.h>
#include <spazer/spazer.h>
#include <string>

namespace fleed {

class Ui
{
public:
  Ui();
  ~Ui();

  Ui(const Ui &)            = delete;
  Ui &operator=(const Ui &) = delete;

  /**
   * @brief Bring up ncurses and create the editor panel.
   * @param header Title shown in the panel's top border.
   * @return @c true on success, @c false if the terminal or panel could not
   *         be created (the UI is then fully torn down).
   */
  bool init(const std::string &header);

  /** @brief Tear ncurses down. Idempotent and safe to call from the destructor. */
  void shutdown();

  /**
   * @brief Block until one keystroke is available.
   * @param out Receives the keystroke (codepoint or KEY_* constant).
   * @return @c OK for a regular character, @c KEY_CODE_YES for a function key,
   *         @c ERR on failure.
   */
  int readKey(wint_t &out);

  /**
   * @brief Repaint the editor panel from scratch with the given text.
   * @param text Full buffer contents to display.
   */
  void redraw(const std::string &text);

  /**
   * @brief Append one character at the cursor and flush the panel.
   * @param ch Wide character to draw (codepoint).
   */
  void putChar(wchar_t ch);

  /**
   * @brief Replace the centre slot of the bottom status bar.
   * @param text Status text, or @c nullptr to clear the slot.
   */
  void setStatus(const char *text);

private:
  SCREEN      *m_scr    = nullptr;
  spz_panel_t *m_editor = nullptr;
};

} /* namespace fleed */
