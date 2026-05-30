/**
 * @file fleed/src/Ui.cpp
 * @brief Spazer-backed view layer for the editor — owns the SCREEN, the
 *        scrollable pad, and the status-bar string.
 */

#include <fleed/Ui.hpp>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace fleed {

namespace {

/** Right-hand text shown in every status bar variant. */
constexpr const char *kHints = "F1 quit F2 save";

/** Horizontal headroom (columns) reserved past the longest line so a few
 *  inserts at end-of-line don't force a pad resize. */
constexpr int kPadWidthPad = 16;

/** Minimum virtual pad rows / cols at creation time — avoids one resize on
 *  the first redraw for typical small files. */
constexpr int kPadMinRows = 32;
constexpr int kPadMinCols = 80;

/**
 * @brief Push the current bottom status bar with a centre slot.
 *
 * @param center  Centre text, NULL for empty.
 */
void paintStatus(const char *center)
{
  spz_bar_t bar = {"fleed", center, kHints, SPZ_STYLE_STATUS};
  spz_statusbar(&bar);
}

} /* namespace */

Ui::Ui() = default;

Ui::~Ui()
{
  shutdown();
}

bool Ui::init(const std::string &header)
{
  const char *term = std::getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  (void)std::setvbuf(stdout, nullptr, _IONBF, 0);

  m_scr = newterm(term, stdout, stdin);
  if(!m_scr)
    return false;
  set_term(m_scr);
  typeahead(-1);

  if(spz_init() != 0) {
    shutdown();
    return false;
  }

  int rows = 0, cols = 0;
  getmaxyx(stdscr, rows, cols);

  /* Reserve the bottom row for the status bar. */
  spz_rect_t outer = {0, 0, rows - 1, cols};
  spz_rect_t virt  = {
      0, 0, (kPadMinRows > rows ? kPadMinRows : rows),
      (kPadMinCols > cols ? kPadMinCols : cols)
  };
  m_pad = spz_pad_new(outer, virt, header.c_str(), SPZ_BORDER_SINGLE);
  if(!m_pad) {
    shutdown();
    return false;
  }
  keypad(spz_pad_buffer(m_pad), TRUE);

  paintStatus(nullptr);
  spz_pad_refresh(m_pad);
  return true;
}

void Ui::shutdown()
{
  if(m_pad) {
    spz_pad_del(m_pad);
    m_pad = nullptr;
  }
  if(m_scr) {
    spz_shutdown();
    endwin();
    delscreen(m_scr);
    m_scr = nullptr;
  }
}

int Ui::pollEvent(spz_event_t &out)
{
  return spz_poll(spz_pad_buffer(m_pad), &out);
}

void Ui::ensurePadSize(const Buffer &buffer)
{
  size_t max_w = 0;
  for(size_t i = 0; i < buffer.lineCount(); ++i) {
    size_t w = buffer.line(i).size();
    if(w > max_w)
      max_w = w;
  }
  int need_rows = static_cast<int>(buffer.lineCount()) + 1;
  int need_cols = static_cast<int>(max_w) + kPadWidthPad;
  (void)spz_pad_resize(m_pad, need_rows, need_cols);
}

void Ui::redraw(const Buffer &buffer)
{
  ensurePadSize(buffer);
  WINDOW *body = spz_pad_buffer(m_pad);
  werase(body);
  for(size_t i = 0; i < buffer.lineCount(); ++i)
    mvwaddstr(body, static_cast<int>(i), 0, buffer.line(i).c_str());
  refreshCursor(buffer);
}

void Ui::redrawLine(const Buffer &buffer, size_t line_idx)
{
  ensurePadSize(buffer);
  WINDOW *body = spz_pad_buffer(m_pad);
  wmove(body, static_cast<int>(line_idx), 0);
  wclrtoeol(body);
  mvwaddstr(body, static_cast<int>(line_idx), 0, buffer.line(line_idx).c_str());
}

void Ui::refreshCursor(const Buffer &buffer)
{
  spz_pad_set_cursor(
      m_pad, static_cast<int>(buffer.cursor().y),
      static_cast<int>(buffer.cursor().x)
  );
  refreshStatusBar(buffer);
  spz_pad_refresh(m_pad);
}

void Ui::setStatus(const char *text)
{
  m_status_msg = text ? text : "";
  paintStatus(m_status_msg.empty() ? nullptr : m_status_msg.c_str());
}

void Ui::refreshStatusBar(const Buffer &buffer)
{
  char pos[32];
  (void)std::snprintf(
      pos, sizeof(pos), "L:%zu C:%zu", buffer.cursor().y + 1,
      buffer.cursor().x + 1
  );
  paintStatus(pos);
}

} /* namespace fleed */
