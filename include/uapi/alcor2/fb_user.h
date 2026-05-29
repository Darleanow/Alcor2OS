/**
 * @file include/uapi/alcor2/fb_user.h
 * @brief Userland helpers for the Alcor2 custom framebuffer syscalls.
 *
 * Thin inline wrappers around @c SYS_ALCOR_FB_INFO and @c SYS_ALCOR_FB_MMAP.
 * Include this header in userspace programs that need direct framebuffer
 * access (renderers, compositors, graphics demos).
 *
 * Requires a libc with @c syscall() — musl is the tested implementation.
 * Compile with SSE enabled for x86_64 float ABI compatibility with stb /
 * FreeType code.
 */

#ifndef ALCOR2_ALCOR_FB_USER_H
#define ALCOR2_ALCOR_FB_USER_H

#include <alcor2/fb.h>
#include <alcor2/syscall.h> /* SYS_ALCOR_FB_INFO / MMAP — single source of truth. */
#include <sys/syscall.h>
#include <unistd.h>

/**
 * @brief Query framebuffer geometry (dimensions, pitch, pixel offset).
 * @param info Output buffer filled with @ref alcor_fb_info_t.
 * @return 0 on success, negative errno on failure.
 */
static inline long alcor_fb_info(alcor_fb_info_t *info)
{
  return syscall(SYS_ALCOR_FB_INFO, info);
}

/**
 * @brief Map the linear framebuffer into the calling process's address space.
 *
 * @param addr_or_zero  Preferred virtual address hint (0 = kernel chooses).
 * @param size_or_zero  Mapping size hint in bytes (0 = full framebuffer span).
 * @return Pointer to the first mapped byte, or @c (void*)-1 on error.
 */
static inline void *
    alcor_fb_mmap_hint(unsigned long addr_or_zero, unsigned long size_or_zero)
{
  long r = syscall(SYS_ALCOR_FB_MMAP, addr_or_zero, size_or_zero);
  if(r < 0)
    return (void *)-1;
  return (void *)r;
}

/**
 * @brief Map the full framebuffer at a kernel-chosen address.
 * @return Pointer to the first pixel, or @c (void*)-1 on error.
 */
static inline void *alcor_fb_mmap(void)
{
  return alcor_fb_mmap_hint(0, 0);
}

#endif
