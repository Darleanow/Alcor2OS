/**
 * @file apps/shell/platform/atlas_stub.c
 * @brief Stub used when FreeType isn't built. Kernel keeps the CP437 bitmap
 * fallback when atlas_submit returns -1.
 */

#include <shell/atlas.h>

int atlas_submit(const char *font_path)
{
  (void)font_path;
  return -1;
}
