/**
 * @file include/alcor2/alcor_fb.h
 * @brief Userspace API: Limine linear framebuffer info and mmap helpers.
 *
 * Syscalls @ref SYS_ALCOR_FB_INFO and @ref SYS_ALCOR_FB_MMAP expose the same
 * memory the kernel console uses; drawing from userland is allowed and shares
 * the scanout buffer (coordinate with kernel writes).
 */

#ifndef ALCOR2_ALCOR_FB_H
#define ALCOR2_ALCOR_FB_H

#include <alcor2/types.h>

/** @brief Linear framebuffer description (fixed layout for syscall ABI). */
typedef struct PACKED
{
  u32 width;
  u32 height;
  u32 pitch;
  u16 bpp;
  u16 _pad;
  /** @brief Active bytes: @c pitch * height (may be less than @a map_size). */
  u64 byte_len;
  /** @brief Mappable span in bytes (page-aligned, includes leading padding). */
  u64 map_size;
} alcor_fb_info_t;

#endif
