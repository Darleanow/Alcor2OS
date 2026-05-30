/**
 * @file fleed/Buffer.hpp
 * @brief Text buffer with file load/save and cursor-aware mutation.
 *
 * Currently stores text as one @c std::string per line in a @c std::vector.
 * The public API is the seam where richer representations (gap buffer, rope,
 * undo stack) will plug in later.
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fleed {

/** @brief Cursor position in the buffer. */
struct Cursor
{
  size_t y = 0; /**< Line index, 0-based. */
  size_t x = 0; /**< Byte offset inside the line, 0-based. */
};

class Buffer
{
public:
  /**
   * @brief Load a file's contents into the buffer.
   *
   * The buffer always ends up with at least one (possibly empty) line on
   * success — newline-terminated and empty files both produce a one-line
   * empty buffer, so the caller does not have to special-case @c lineCount.
   *
   * @param path  File to read.
   * @return      @c true on success; @c false if the file cannot be opened
   *              (the buffer is then left empty).
   */
  bool load(const std::filesystem::path &path);

  /**
   * @brief Write the buffer back to disk, overwriting the target file.
   *
   * Each line is followed by @c '\n', including the last one.
   *
   * @param path  Destination file.
   * @return      @c true on success, @c false on any I/O error.
   */
  bool save(const std::filesystem::path &path) const;

  /**
   * @brief Insert @p n bytes at the cursor and advance the cursor past them.
   *
   * @param data  Pointer to the bytes to copy.
   * @param n     Byte count.
   */
  void append(const char *data, size_t n);

  /**
   * @brief Split the current line at the cursor and move the cursor to the
   *        start of the new line below.
   */
  void newLine();

  /**
   * @brief Delete the byte to the left of the cursor, or fuse with the
   *        previous line when the cursor is at column 0.
   *
   * @return @c true if anything was removed; @c false when the cursor was
   *         already at the very start of the document.
   */
  bool popBack();

  /**
   * @brief Read-only access to one line.
   *
   * @param row  Line index. Must be < @ref lineCount.
   * @return     The line's contents.
   */
  const std::string &line(size_t row) const;

  /** @brief Total line count. */
  size_t        lineCount() const;

  /** @brief @c true if the buffer holds no lines. */
  bool          empty() const noexcept;

  /** @brief Current cursor position. */
  const Cursor &cursor() const noexcept;

  /**
   * @brief Set the cursor to an absolute position. No bounds checking.
   *
   * @param x  Byte offset inside the target line.
   * @param y  Line index.
   */
  void          setCursorPos(size_t x, size_t y);

  /** @brief Move the cursor one line up, clamping the column if needed. */
  void          cursorMoveUp();
  /** @brief Move the cursor one line down, clamping the column if needed. */
  void          cursorMoveDown();
  /** @brief Move the cursor one byte left, wrapping to the previous line. */
  void          cursorMoveLeft();
  /** @brief Move the cursor one byte right, wrapping to the next line. */
  void          cursorMoveRight();
  /** @brief Move the cursor to the first byte of the current line. */
  void          cursorMoveLineStart();
  /** @brief Move the cursor past the last byte of the current line. */
  void          cursorMoveLineEnd();

private:
  std::vector<std::string> m_text;
  Cursor                   m_cursor;
};

} /* namespace fleed */
