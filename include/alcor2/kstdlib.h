/**
 * @file include/alcor2/kstdlib.h
 * @brief Kernel micro standard library.
 *
 * Provides common utility functions for kernel code:
 * - Memory operations (copy, zero, set, compare)
 * - String operations (length, copy, compare, find)
 * 
 * These replace the duplicated static functions across FS drivers.
 */

#ifndef ALCOR2_KSTDLIB_H
#define ALCOR2_KSTDLIB_H

#include <alcor2/types.h>

/**
 * @brief Copy memory region.
 * @param dst Destination.
 * @param src Source.
 * @param n Byte count.
 * @return dst pointer.
 */
void *kmemcpy(void *dst, const void *src, u64 n);

/**
 * @brief Set memory to value.
 * @param dst Destination.
 * @param val Value to set (converted to u8).
 * @param n Byte count.
 * @return dst pointer.
 */
void *kmemset(void *dst, int val, u64 n);

/**
 * @brief Zero memory region.
 * @param dst Destination.
 * @param n Byte count.
 */
void kzero(void *dst, u64 n);

/**
 * @brief Compare memory regions.
 * @param s1 First region.
 * @param s2 Second region.
 * @param n Byte count.
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2.
 */
int kmemcmp(const void *s1, const void *s2, u64 n);

/**
 * @brief Get string length.
 * @param s String.
 * @return Length (not including null terminator).
 */
u64 kstrlen(const char *s);

/**
 * @brief Copy string with maximum length.
 * @param dst Destination.
 * @param src Source.
 * @param max Maximum bytes including null terminator.
 * @return dst pointer.
 */
char *kstrncpy(char *dst, const char *src, u64 max);

/**
 * @brief Compare two strings.
 * @param a First string.
 * @param b Second string.
 * @return 0 if equal, <0 if a<b, >0 if a>b.
 */
int kstrcmp(const char *a, const char *b);

/**
 * @brief Compare two strings (case insensitive).
 * @param a First string.
 * @param b Second string.
 * @return 0 if equal.
 */
int kstricmp(const char *a, const char *b);

/**
 * @brief Check if strings are equal.
 * @param a First string.
 * @param b Second string.
 * @return true if equal.
 */
bool kstreq(const char *a, const char *b);

/**
 * @brief Find character in string.
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to character, or NULL if not found.
 */
char *kstrchr(const char *s, int c);

/**
 * @brief Find last occurrence of character in string.
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to character, or NULL if not found.
 */
char *kstrrchr(const char *s, int c);

/**
 * @brief Check if string starts with prefix.
 * @param str String to check.
 * @param prefix Prefix to look for.
 * @return true if str starts with prefix.
 */
bool kstarts_with(const char *str, const char *prefix);

/**
 * @brief Convert character to uppercase.
 * @param c Character.
 * @return Uppercase version.
 */
int ktoupper(int c);

/**
 * @brief Convert character to lowercase.
 * @param c Character.
 * @return Lowercase version.
 */
int ktolower(int c);

#endif /* ALCOR2_KSTDLIB_H */
