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
#include <fleed/Buffer.hpp>
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

  /** @brief Tear ncurses down. Idempotent and safe to call from the destructor.
   */
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
   * @param buffer Full buffer contents to display.
   */
  void redraw(const Buffer &buffer);

  /**
   * @brief Replace the centre slot of the bottom status bar.
   * @param text Status text, or @c nullptr to clear the slot.
   */
  void setStatus(const char *text);

  /**
   * @brief Redraw a single line in-place without flushing to screen.
   *        Call redrawCursor() afterwards to trigger the actual update.
   * @param buffer Buffer to read line content from.
   * @param line_idx Absolute line index in the buffer.
   */
  void redrawLine(const Buffer &buffer, size_t line_idx);

  /**
   * @brief Redraws cursor at current position and flushes to screen.
   * @param buffer The text buffer containing the cursor.
   */
  void redrawCursor(const Buffer &buffer);

private:
  void        refreshStatus(const Buffer &buffer);

  SCREEN      *m_scr          = nullptr;
  spz_panel_t *m_editor       = nullptr;
  size_t       m_scroll_offset = 0;
};

} /* namespace fleed */
