/**
 * @file include/alcor2/alcor_fb_user.h
 * @brief Userland helpers for @ref alcor_fb_info_t and framebuffer mmap.
 *
 * Requires musl (or any libc with @c syscall()). stb_truetype / FreeType demos
 * should compile C++ font code with SSE enabled (x86_64 float ABI). On the
 * guest, place @c FiraCode-Regular.ttf (or any TTF/OTF) under @c /bin/ and run
 * @c font-demo.
 */

#ifndef ALCOR2_ALCOR_FB_USER_H
#define ALCOR2_ALCOR_FB_USER_H

#include <alcor2/alcor_fb.h>
#include <sys/syscall.h>
#include <unistd.h>

/** @{ Syscall numbers (must match kernel @c syscall.h). */
#ifndef SYS_ALCOR_FB_INFO
  #define SYS_ALCOR_FB_INFO 498
#endif
#ifndef SYS_ALCOR_FB_MMAP
  #define SYS_ALCOR_FB_MMAP 499
#endif
/** @} */

static inline long alcor_fb_info(alcor_fb_info_t *info)
{
  return syscall(SYS_ALCOR_FB_INFO, info);
}

/** @return User pointer to first pixel, or @c (void *)-1 on error. */
static inline void *
    alcor_fb_mmap_hint(unsigned long addr_or_zero, unsigned long size_or_zero)
{
  long r = syscall(SYS_ALCOR_FB_MMAP, addr_or_zero, size_or_zero);
  if(r < 0)
    return (void *)-1;
  return (void *)r;
}

static inline void *alcor_fb_mmap(void)
{
  return alcor_fb_mmap_hint(0, 0);
}

#endif
