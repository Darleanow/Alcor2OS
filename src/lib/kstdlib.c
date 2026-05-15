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
 * @brief Compare two memory regions.
 *
 * @param s1 First buffer.
 * @param s2 Second buffer.
 * @param n  Number of bytes to compare.
 * @return 0 if equal, negative if s1 < s2, positive if s1 > s2.
 */
// cppcheck-suppress unusedFunction
int kmemcmp(const void *s1, const void *s2, u64 n)
{
  const u8 *p1 = (const u8 *)s1;
  const u8 *p2 = (const u8 *)s2;
  while(n--) {
    if(*p1 != *p2)
      return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
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
 * @brief Case-insensitive string comparison.
 *
 * @param a First string.
 * @param b Second string.
 * @return 0 if equal (ignoring case), non-zero otherwise.
 */
// cppcheck-suppress unusedFunction
int kstricmp(const char *a, const char *b)
{
  while(*a && *b) {
    int ca = ktolower((u8)*a);
    int cb = ktolower((u8)*b);
    if(ca != cb)
      return ca - cb;
    a++;
    b++;
  }
  return ktolower((u8)*a) - ktolower((u8)*b);
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
 * @brief Find the first occurrence of a character in a string.
 *
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to the character, or NULL if not found.
 */
// cppcheck-suppress unusedFunction
char *kstrchr(const char *s, int c)
{
  while(*s) {
    if(*s == (char)c)
      return (char *)s;
    s++;
  }
  return (c == '\0') ? (char *)s : NULL;
}

/**
 * @brief Find the last occurrence of a character in a string.
 *
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to the last occurrence, or NULL if not found.
 */
// cppcheck-suppress unusedFunction
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
 * @brief Check if a string starts with a prefix.
 *
 * @param str    String to check.
 * @param prefix Prefix to test for.
 * @return true if str starts with prefix.
 */
// cppcheck-suppress unusedFunction
bool kstarts_with(const char *str, const char *prefix)
{
  while(*prefix) {
    if(*str != *prefix)
      return false;
    str++;
    prefix++;
  }
  return true;
}

/**
 * @brief Convert a character to uppercase.
 *
 * @param c Character to convert.
 * @return Uppercase character, or c if not a letter.
 */
// cppcheck-suppress unusedFunction
int ktoupper(int c)
{
  if(c >= 'a' && c <= 'z')
    return c - 32;
  return c;
}

/**
 * @brief Convert a character to lowercase.
 *
 * @param c Character to convert.
 * @return Lowercase character, or c if not a letter.
 */
int ktolower(int c)
{
  if(c >= 'A' && c <= 'Z')
    return c + 32;
  return c;
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
 * @brief Concatenate strings with maximum length.
 * @param dst Destination.
 * @param src Source.
 * @param max Maximum bytes to add.
 * @return dst pointer.
 */
char *kstrncat(char *dst, const char *src, u64 max)
{
  u64 len = kstrlen(dst);
  u64 i;
  for(i = 0; i < max && src[i] != '\0'; i++) {
    dst[len + i] = src[i];
  }
  dst[len + i] = '\0';
  return dst;
}
