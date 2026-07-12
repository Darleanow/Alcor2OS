/**
 * @file fleed/Editor.hpp
 * @brief Top-level editor: owns the buffer, the UI, and runs the event loop.
 */
#pragma once

#include <fleed/Buffer.hpp>
#include <fleed/Ui.hpp>

#include <filesystem>

namespace fleed {

class Editor
{
public:
  /**
   * @brief Build an editor bound to a target file (not opened yet).
   * @param path Path to the file the editor will load and save.
   */
  explicit Editor(std::filesystem::path path);

  /**
   * @brief Load the file, bring the UI up, and process keys until quit.
   * @return Process exit code: 0 on clean quit, non-zero on init failure.
   */
  int run();

private:
  /**
   * @brief Insert one codepoint into the buffer and update the display.
   * @param ch Wide character to insert.
   */
  void insertChar(wchar_t ch);

  /** @brief Save the buffer and report the outcome in the status bar. */
  void handleSave();

  std::filesystem::path m_path;
  Buffer                m_buffer;
  Ui                    m_ui;
};

} /* namespace fleed */
