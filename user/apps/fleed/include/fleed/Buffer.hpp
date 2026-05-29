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
#include <vector>

namespace fleed {

struct Cursor
{
  size_t y = 0, x = 0;
};

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
   * @param n length.
   */
  void append(const char *data, size_t n);

  /**
   * @brief Copies from cursor to next line.
   */
  void newLine();

  /**
   * @brief Remove the last byte of the buffer if any.
   * @return @c true if a byte was removed, @c false if the buffer was empty.
   */
  bool popBack();

  /**
   * @brief Get the line corresponding to the @c row.
   * @param row Row index.
   * @return the line of text.
   */
  const std::string &line(size_t row) const;

  /** @brief get total line count.
   * @return total line count.
   */
  size_t lineCount() const;

  /** @brief Whether the buffer holds any bytes. */
  bool empty() const noexcept;

  /** @brief Get the current cursor position.
   * @return the current cursor position.
   */
  const Cursor &cursor() const noexcept;

  /** @brief Sets cursor position at @c y @c x
   * @param y the Y coordinate
   * @param x the X coordinate
   */
  void setCursorPos(size_t x, size_t y);

  /** @brief Moves cursor Up if possible.*/
  void cursorMoveUp();

  /** @brief Moves the cursor Down if possible */
  void cursorMoveDown();

  /** @brief Moves the cursor Left if possible. */
  void cursorMoveLeft();

  /** @brief Moves the cursor Right if possible. */
  void cursorMoveRight();

private:
  std::vector<std::string> m_text;
  Cursor                   m_cursor;
};

} /* namespace fleed */
