/**
 * @file fleed/src/Buffer.cpp
 */

#include <fleed/Buffer.hpp>

#include <fstream>
#include <sstream>

namespace fleed {

bool Buffer::load(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    m_text.clear();
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  m_text = ss.str();
  return true;
}

bool Buffer::save(const std::filesystem::path &path) const
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if(!out)
    return false;
  out.write(m_text.data(), static_cast<std::streamsize>(m_text.size()));
  return out.good();
}

void Buffer::append(const char *data, std::size_t n)
{
  m_text.append(data, n);
}

bool Buffer::popBack()
{
  if(m_text.empty())
    return false;
  m_text.pop_back();
  return true;
}

const std::string &Buffer::text() const noexcept
{
  return m_text;
}

std::size_t Buffer::size() const noexcept
{
  return m_text.size();
}

bool Buffer::empty() const noexcept
{
  return m_text.empty();
}

} /* namespace fleed */
