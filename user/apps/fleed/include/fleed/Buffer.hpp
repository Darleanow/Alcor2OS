/**
 * @file fleed/Buffer.hpp
 * @brief Text buffer with file load/save.
 *
 * Currently a flat std::string; the API is the seam where richer
 * representations (gap buffer, rope, undo stack) will plug in later.
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace fleed {

class Buffer
{
public:
  /**
   * @brief Load a file's contents into the buffer.
   * @param path File to read.
   * @return @c true on success; @c false if the file cannot be opened
   *         (the buffer is then left empty).
   */
  bool load(const std::filesystem::path &path);

  /**
   * @brief Truncate the file and write the buffer to it.
   * @param path Destination file (overwritten).
   * @return @c true on success, @c false on any I/O error.
   */
  bool save(const std::filesystem::path &path) const;

  /**
   * @brief Append raw bytes at the end of the buffer.
   * @param data Pointer to the bytes to copy.
   * @param n    Number of bytes to copy from @p data.
   */
  void append(const char *data, std::size_t n);

  /**
   * @brief Remove the last byte of the buffer if any.
   * @return @c true if a byte was removed, @c false if the buffer was empty.
   */
  bool popBack();

  /** @brief Read-only view of the underlying text. */
  const std::string &text() const noexcept;

  /** @brief Number of bytes currently held. */
  std::size_t size() const noexcept;

  /** @brief Whether the buffer holds any bytes. */
  bool empty() const noexcept;

private:
  std::string m_text;
};

} /* namespace fleed */
