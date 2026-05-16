/**
 * @file src/lib/kstdlib.c
 * @brief Kernel micro-library implementation (strings, memory).
 */

#include <alcor2/kstdlib.h>

/**
 * @brief Copy memory from src to dst.
 *
 * Uses REP MOVSB which on modern x86-64 CPUs (ERMSB) runs at full memory
 * bandwidth via microcode — equivalent to a tuned libc memcpy.
 *
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param n   Number of bytes to copy.
 * @return Pointer to dst.
 */
void *kmemcpy(void *dst, const void *src, u64 n)
{
  // cppcheck-suppress constVariablePointer
  void       *d = dst;
  const void *s = src;
  __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n)::"memory");
  return dst;
}

/**
 * @brief Fill memory with a byte value.
 *
 * Uses REP STOSB which on modern x86-64 CPUs runs at full memory bandwidth.
 *
 * @param dst Destination buffer.
 * @param val Byte value to fill with.
 * @param n   Number of bytes to set.
 * @return Pointer to dst.
 */
void *kmemset(void *dst, int val, u64 n)
{
  // cppcheck-suppress constVariablePointer
  void *d = dst;
  __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"((u8)val) : "memory");
  return dst;
}

/**
 * @brief Zero-fill a memory region.
 *
 * Same @c rep stosb sequence as @c kmemset(..., 0, n) without an extra function
 * call.
 *
 * @param dst Destination buffer.
 * @param n   Number of bytes to zero.
 */
void kzero(void *dst, u64 n)
{
  // cppcheck-suppress constVariablePointer
  void *d = dst;
  __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"((u8)0) : "memory");
}

/**
 * @brief Get the length of a null-terminated string.
 *
 * @param s String to measure.
 * @return Length in bytes (excluding null terminator).
 */
u64 kstrlen(const char *s)
{
  u64 len = 0;
  while(s[len])
    len++;
  return len;
}

/**
 * @brief Copy a string with a size limit.
 *
 * @param dst Destination buffer.
 * @param src Source string.
 * @param max Maximum buffer size (including null terminator).
 * @return Pointer to dst.
 */
char *kstrncpy(char *dst, const char *src, u64 max)
{
  u64 i;
  for(i = 0; i < max - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = '\0';
  return dst;
}

/**
 * @brief Compare two strings lexicographically.
 *
 * @param a First string.
 * @param b Second string.
 * @return 0 if equal, negative if a < b, positive if a > b.
 */
int kstrcmp(const char *a, const char *b)
{
  while(*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (u8)*a - (u8)*b;
}

/**
 * @brief Check if two strings are equal.
 *
 * @param a First string.
 * @param b Second string.
 * @return true if equal, false otherwise.
 */
bool kstreq(const char *a, const char *b)
{
  return kstrcmp(a, b) == 0;
}

/**
 * @brief Find the last occurrence of a character in a string.
 *
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to the last occurrence, or NULL if not found.
 */
char *kstrrchr(const char *s, int c)
{
  const char *last = NULL;
  while(*s) {
    if(*s == (char)c)
      last = s;
    s++;
  }
  return (c == '\0') ? (char *)s : (char *)last;
}

/**
 * @brief Compare two strings up to n characters.
 * @param a First string.
 * @param b Second string.
 * @param n Maximum count.
 * @return 0 if equal.
 */
int kstrncmp(const char *a, const char *b, u64 n)
{
  if(n == 0)
    return 0;
  while(n-- > 0 && *a && (*a == *b)) {
    if(n == 0)
      return 0;
    a++;
    b++;
  }
  return *(unsigned char *)a - *(unsigned char *)b;
}

/**
 * @brief Bounded strcat (BSD @c strlcat semantics).
 *
 * Scans @p dst only up to @p dst_cap - 1 so callers need not pre-trust NUL
 * placement when sizing is authoritative.
 */
u64 kstrlcat(char *dst, const char *src, u64 dst_cap)
{
  u64 dlen;

  if(dst_cap == 0)
    return kstrlen(src);

  dlen = 0;
  while(dlen < dst_cap && dst[dlen] != '\0')
    dlen++;

  if(dlen >= dst_cap)
    return dst_cap + kstrlen(src);

  u64 space = dst_cap - dlen - 1;
  u64 i     = 0;
  while(i < space && src[i] != '\0') {
    dst[dlen + i] = src[i];
    i++;
  }
  dst[dlen + i] = '\0';

  return dlen + kstrlen(src);
}
