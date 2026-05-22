/**
 * @file include/alcor2/fb_console_ioctl.h
 * @brief Userspace-facing ioctl interface for the framebuffer console.
 *
 * Safe to include from userland — no kernel-only types or functions.
 */

#ifndef ALCOR2_FB_CONSOLE_IOCTL_H
#define ALCOR2_FB_CONSOLE_IOCTL_H

#include <stdint.h>

/* ioctls on fd 1/2 — Linux-style _IOC encoding. Group byte 'F' = fb console. */

/** SET_ATLAS: submit a glyph atlas. arg = fb_console_atlas_t*. */
#define FB_CONSOLE_SET_ATLAS                                                   \
  ((1U << 30) | ((unsigned)'F' << 8) | 1U | (sizeof(fb_console_atlas_t) << 16))

/** YIELD: release the framebuffer for raw mmap use. arg ignored. */
#define FB_CONSOLE_YIELD ((unsigned)('F' << 8) | 2U)

/** RECLAIM: resume kernel rendering, repaint the grid. arg ignored. */
#define FB_CONSOLE_RECLAIM ((unsigned)('F' << 8) | 3U)

/**
 * @brief Atlas descriptor submitted by userspace via FB_CONSOLE_SET_ATLAS.
 */
typedef struct
{
  uint64_t pixels_user;  /**< Userspace VA of the glyph atlas pixel data. */
  uint32_t pixels_size;  /**< Total atlas bytes. */
  uint32_t cell_w;       /**< Glyph cell width in pixels. */
  uint32_t cell_h;       /**< Glyph cell height in pixels. */
  uint32_t stride_bytes; /**< Bytes per row of a single cell. */
  uint32_t bpp;          /**< Atlas bpp — must match framebuffer. */
  uint32_t n_glyphs;     /**< Total glyph slots in the atlas. */
  uint64_t cp_map_user; /**< Userspace VA of u32[n_cp] codepoint→glyph_idx. */
  uint32_t n_cp;        /**< Size of cp_map (covers codepoints 0..n_cp-1). */
  uint32_t fallback_idx;  /**< Glyph for unmapped codepoints. */
  uint32_t bold_offset;   /**< First bold glyph slot; 0 = no bold atlas. */
  uint32_t italic_offset; /**< First italic glyph slot; 0 = no italic atlas. */
} fb_console_atlas_t;

#endif /* ALCOR2_FB_CONSOLE_IOCTL_H */
