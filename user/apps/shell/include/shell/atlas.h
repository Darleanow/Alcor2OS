/**
 * @file shell/atlas.h
 * @brief Rasterize a TTF (Fira Code) into a glyph atlas and submit it to the
 * kernel framebuffer console via @c ioctl(FB_CONSOLE_SET_ATLAS).
 */

#ifndef SHELL_ATLAS_H
#define SHELL_ATLAS_H

/**
 * @brief Load the TTF at @p font_path, rasterize ASCII + box-drawing + a few
 * Latin-1 glyphs, and submit the atlas to the kernel.
 *
 * @return 0 on success, -1 if the font file cannot be opened, FreeType setup
 *         fails, or the kernel rejects the atlas (in which case the kernel
 *         keeps its built-in CP437 bitmap fallback — the shell is still
 *         usable, just without Fira).
 */
int atlas_submit(const char *font_path);

#endif /* SHELL_ATLAS_H */
