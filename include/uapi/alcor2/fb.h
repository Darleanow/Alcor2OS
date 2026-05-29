/**
 * @file include/uapi/alcor2/fb.h
 * @brief Userspace API: Limine linear framebuffer info and mmap helpers.
 *
 * Syscalls @ref SYS_ALCOR_FB_INFO and @ref SYS_ALCOR_FB_MMAP expose the same
 * memory the kernel console uses; drawing from userland is allowed and shares
 * the scanout buffer (coordinate with kernel writes).
 */

#ifndef ALCOR2_ALCOR_FB_H
#define ALCOR2_ALCOR_FB_H

#include <stdint.h>

/** @brief Linear framebuffer description (fixed layout for syscall ABI). */
typedef struct __attribute__((packed))
{
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint16_t bpp;
  uint16_t _pad;
  /** @brief Active bytes: @c pitch * height (may be less than @a map_size). */
  uint64_t byte_len;
  /** @brief Mappable span in bytes (page-aligned, includes leading padding). */
  uint64_t map_size;
} alcor_fb_info_t;

#endif
