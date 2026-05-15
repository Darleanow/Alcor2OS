/**
 * @file src/lib/compiler_abi.c
 * @brief Compiler ABI built-in shims.
 *
 * Compilers (like clang and gcc) will occasionally emit calls to standard
 * library functions like memcpy, memset, and memmove, even when compiling
 * with -ffreestanding. This happens for struct copies, aggregate zero-init,
 * and similar compiler-generated code.
 *
 * This file provides these essential ABI symbols to satisfy the linker
 * (ld.lld -nostdlib), forwarding them to our internal k-variants which
 * are optimized with inline assembly (REP MOVSB / REP STOSB).
 */

#include <alcor2/kstdlib.h>
#include <alcor2/types.h>

void *memcpy(void *restrict dst, const void *restrict src, u64 n)
{
  return kmemcpy(dst, src, n);
}

void *memset(void *dst, int c, u64 n)
{
  return kmemset(dst, c, n);
}

void *memmove(void *dst, const void *src, u64 n)
{
  const u8 *s = (const u8 *)src;
  u8       *d = (u8 *)dst;

  if(d < s || d >= s + n) {
    /* No overlap, or dst is before src (safe for forward copy) */
    return kmemcpy(dst, src, n);
  }

  /* Backward copy for overlapping dst > src case */
  d += n;
  s += n;
  while(n--) {
    *--d = *--s;
  }
  return dst;
}
