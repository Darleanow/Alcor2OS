/**
 * @file fleed/src/Buffer.cpp
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
  while(std::getline(in, line)) {
    m_text.push_back(line);
  }

  if(m_text.empty()) {
    m_text.push_back("");
  }

  return true;
}

bool Buffer::save(const std::filesystem::path &path) const
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  std::string   buf;
  for(const auto &string : m_text) {
    buf.append(string + '\n');
  }

  if(!out)
    return false;

  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  return out.good();
}

void Buffer::append(const char *data, size_t n)
{
  m_text.at(m_cursor.y).insert(m_cursor.x, data, n);
  setCursorPos(m_cursor.x + n, m_cursor.y);
}

bool Buffer::popBack()
{
  if(m_text.empty())
    return false;

  if(m_text.at(m_cursor.y).empty())
    return false;

  if(m_cursor.x == 0 && m_cursor.y == 0) {
    return false;
  }

  if(m_cursor.x == 0) {
    std::string sline = line(m_cursor.y);

    size_t      fuse_point = m_text.at(m_cursor.y - 1).size();

    m_text.at(m_cursor.y - 1).append(sline);
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
  if(m_cursor.x == m_text.at(m_cursor.y).size() &&
     m_cursor.y + 1 >= m_text.size())
    return;

  if(m_cursor.x == m_text.at(m_cursor.y).size()) {
    ++m_cursor.y;
    m_cursor.x = 0;
    return;
  }

  ++m_cursor.x;
}
} /* namespace fleed */
