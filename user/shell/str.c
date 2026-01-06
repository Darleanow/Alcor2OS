/**
 * Alcor2 Shell - String Utilities
 */

#include "shell.h"
#include <stddef.h>

/**
 * @brief Calculate string length
 * @param s Null-terminated string
 * @return Length in bytes (excluding null terminator)
 */
size_t sh_strlen(const char *s)
{
  size_t len = 0;
  while(s[len])
    len++;
  return len;
}

/**
 * @brief Compare two strings
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int sh_strcmp(const char *s1, const char *s2)
{
  while(*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

/**
 * @brief Compare two strings up to n characters
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int sh_strncmp(const char *s1, const char *s2, size_t n)
{
  while(n && *s1 && *s1 == *s2) {
    s1++;
    s2++;
    n--;
  }
  if(n == 0)
    return 0;
  return (unsigned char)*s1 - (unsigned char)*s2;
}

/**
 * @brief Copy string from src to dst
 * @param dst Destination buffer
 * @param src Source string
 * @return Pointer to dst
 */
char *sh_strcpy(char *dst, const char *src)
{
  char *d = dst;
  while((*d++ = *src++))
    ;
  return dst;
}

/**
 * @brief Concatenate src onto dst
 * @param dst Destination buffer (must have space)
 * @param src Source string to append
 * @return Pointer to dst
 */
char *sh_strcat(char *dst, const char *src)
{
  char *d = dst;
  while(*d)
    d++;
  while((*d++ = *src++))
    ;
  return dst;
}
