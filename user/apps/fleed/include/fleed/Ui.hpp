/**
 * @file fleed/Ui.hpp
 * @brief Spazer-based view layer for the editor.
 *
 * Owns the ncurses SCREEN and a scrollable pad sized to the buffer. The rest
 * of the editor never touches ncurses or spazer types directly — it gets
 * decoded @ref spz_event_t events through @ref pollEvent and asks the view
 * to repaint via @ref redraw / @ref redrawLine / @ref refreshCursor.
 */
#pragma once

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
   * @brief Bring up ncurses and create the editor pad.
   * @param header  Title shown in the top border.
   * @return @c true on success, @c false if the terminal or pad could not be
   *         created (the UI is then fully torn down).
   */
  bool init(const std::string &header);

  /** @brief Tear ncurses down. Idempotent. */
  void shutdown();

  /**
   * @brief Block until one decoded event is available.
   * @param out  Receives the event.
   * @return 1 on event, -1 on error.
   */
  int pollEvent(spz_event_t &out);

  /**
   * @brief Repaint the whole buffer into the pad and refresh.
   *
   * Resizes the pad if the buffer has grown taller or wider than the current
   * virtual size.
   *
   * @param buffer  Buffer to project into the pad.
   */
  void redraw(const Buffer &buffer);

  /**
   * @brief Repaint a single line in the pad without refreshing.
   *
   * The caller pairs this with @ref refreshCursor to flush.
   *
   * @param buffer    Buffer to read line content from.
   * @param line_idx  Absolute line index in the buffer.
   */
  void redrawLine(const Buffer &buffer, size_t line_idx);

  /**
   * @brief Move the pad cursor to the buffer cursor and push to the screen.
   *
   * @param buffer  Buffer providing the cursor position and content size.
   */
  void refreshCursor(const Buffer &buffer);

  /**
   * @brief Replace the centre slot of the bottom status bar.
   * @param text  Status text, or @c nullptr to clear.
   */
  void setStatus(const char *text);

private:
  /** Minimum gutter width: one digit plus a trailing separator space. Grows
   *  in @ref ensurePadSize once the line count needs more digits. */
  static constexpr size_t kGutterMin = 2;

  /**
   * @brief Ensure the pad is large enough to host @p rows lines and the
   *        longest line of @p buffer.
   *
   * @param buffer  Source buffer for sizing.
   */
  void ensurePadSize(const Buffer &buffer);

  /**
   * @brief Render a right-aligned, padded 1-based line number for the gutter.
   *
   * The returned string is exactly @ref m_gutter_width columns wide so the
   * buffer text following it lines up across every row.
   *
   * @param line_idx  Absolute 0-based line index in the buffer.
   * @return Gutter cell, e.g. @c " 42 " for index 41.
   */
  std::string gutterCell(size_t line_idx) const;

  /**
   * @brief Repaint the status bar with the current cursor position.
   *
   * @param buffer  Buffer providing the cursor.
   */
  void        refreshStatusBar(const Buffer &buffer);

  SCREEN     *m_scr = nullptr;
  spz_pad_t  *m_pad = nullptr;
  std::string m_status_msg;
  size_t      m_gutter_width = kGutterMin;
};

} /* namespace fleed */
