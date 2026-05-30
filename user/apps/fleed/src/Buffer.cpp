/**
 * @file fleed/src/Buffer.cpp
 * @brief Line-based text buffer with cursor-aware mutation.
 */

#include <fleed/Buffer.hpp>

#include <fstream>

namespace fleed {

bool Buffer::load(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    m_text.clear();
    return false;
  }

  std::string line;
  while(std::getline(in, line))
    m_text.push_back(line);

  /* Keep one empty line so callers can assume lineCount() >= 1. */
  if(m_text.empty())
    m_text.emplace_back();

  return true;
}

bool Buffer::save(const std::filesystem::path &path) const
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if(!out)
    return false;

  std::string buf;
  for(const auto &s : m_text)
    buf.append(s).push_back('\n');

  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  return out.good();
}

void Buffer::append(const char *data, size_t n)
{
  m_text.at(m_cursor.y).insert(m_cursor.x, data, n);
  setCursorPos(m_cursor.x + n, m_cursor.y);
}

void Buffer::newLine()
{
  std::string &curr = m_text.at(m_cursor.y);
  std::string  tail = curr.substr(m_cursor.x);
  curr.erase(
      curr.begin() + static_cast<int>(m_cursor.x), curr.end()
  );
  m_text.insert(m_text.begin() + static_cast<int>(m_cursor.y) + 1, tail);
  setCursorPos(0, m_cursor.y + 1);
}

bool Buffer::popBack()
{
  if(m_text.empty())
    return false;
  if(m_cursor.x == 0 && m_cursor.y == 0)
    return false;

  /* Fuse with the previous line — cursor lands at the junction. */
  if(m_cursor.x == 0) {
    std::string  tail       = m_text.at(m_cursor.y);
    const size_t fuse_point = m_text.at(m_cursor.y - 1).size();
    m_text.at(m_cursor.y - 1).append(tail);
    m_text.erase(m_text.begin() + static_cast<int>(m_cursor.y));
    setCursorPos(fuse_point, m_cursor.y - 1);
    return true;
  }

  m_text.at(m_cursor.y)
      .erase(m_text.at(m_cursor.y).begin() + static_cast<int>(m_cursor.x - 1));
  setCursorPos(m_cursor.x - 1, m_cursor.y);
  return true;
}

const std::string &Buffer::line(size_t row) const
{
  return m_text.at(row);
}

size_t Buffer::lineCount() const
{
  return m_text.size();
}

bool Buffer::empty() const noexcept
{
  return m_text.empty();
}

const Cursor &Buffer::cursor() const noexcept
{
  return m_cursor;
}

void Buffer::setCursorPos(size_t x, size_t y)
{
  m_cursor.x = x;
  m_cursor.y = y;
}

void Buffer::cursorMoveUp()
{
  if(m_cursor.y == 0)
    return;
  --m_cursor.y;
  if(m_cursor.x > m_text.at(m_cursor.y).size())
    m_cursor.x = m_text.at(m_cursor.y).size();
}

void Buffer::cursorMoveDown()
{
  if(m_cursor.y + 1 >= m_text.size())
    return;
  ++m_cursor.y;
  if(m_cursor.x > m_text.at(m_cursor.y).size())
    m_cursor.x = m_text.at(m_cursor.y).size();
}

void Buffer::cursorMoveLeft()
{
  if(m_cursor.x == 0 && m_cursor.y == 0)
    return;
  if(m_cursor.x == 0) {
    --m_cursor.y;
    m_cursor.x = m_text.at(m_cursor.y).size();
    return;
  }
  --m_cursor.x;
}

void Buffer::cursorMoveRight()
{
  const size_t line_size = m_text.at(m_cursor.y).size();
  if(m_cursor.x == line_size && m_cursor.y + 1 >= m_text.size())
    return;
  if(m_cursor.x == line_size) {
    ++m_cursor.y;
    m_cursor.x = 0;
    return;
  }
  ++m_cursor.x;
}

void Buffer::cursorMoveLineStart()
{
  m_cursor.x = 0;
}

void Buffer::cursorMoveLineEnd()
{
  m_cursor.x = m_text.at(m_cursor.y).size();
}

} /* namespace fleed */
