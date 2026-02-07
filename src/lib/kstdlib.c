/**
 * @file src/kernel/kstdlib.c
 * @brief Kernel micro standard library implementation.
 */

#include <alcor2/kstdlib.h>

void *kmemcpy(void *dst, const void *src, u64 n)
{
  u8       *d = (u8 *)dst;
  const u8 *s = (const u8 *)src;
  while(n--)
    *d++ = *s++;
  return dst;
}

void *kmemset(void *dst, int val, u64 n)
{
  u8 *d = (u8 *)dst;
  u8  v = (u8)val;
  while(n--)
    *d++ = v;
  return dst;
}

void kzero(void *dst, u64 n)
{
  kmemset(dst, 0, n);
}

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

u64 kstrlen(const char *s)
{
  u64 len = 0;
  while(s[len])
    len++;
  return len;
}

char *kstrncpy(char *dst, const char *src, u64 max)
{
  u64 i;
  for(i = 0; i < max - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = '\0';
  return dst;
}

int kstrcmp(const char *a, const char *b)
{
  while(*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (u8)*a - (u8)*b;
}

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

bool kstreq(const char *a, const char *b)
{
  return kstrcmp(a, b) == 0;
}

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

// cppcheck-suppress unusedFunction
int ktoupper(int c)
{
  if(c >= 'a' && c <= 'z')
    return c - 32;
  return c;
}

int ktolower(int c)
{
  if(c >= 'A' && c <= 'Z')
    return c + 32;
  return c;
}
