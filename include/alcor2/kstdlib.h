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
 *
 * Always writes a NUL terminator at dst[max-1] if max > 0 (unlike POSIX @c
 * strncpy, which may leave dst non-terminated when strlen(src) >= max).
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
 * @brief Compare two strings up to n characters.
 * @param a First string.
 * @param b Second string.
 * @param n Maximum count.
 * @return 0 if equal.
 */
int kstrncmp(const char *a, const char *b, u64 n);

/**
 * @brief Bounded strcat (BSD @c strlcat semantics).
 *
 * @p dst_cap is the total size of @p dst including the terminating NUL.
 * Appends at most @c dst_cap - strlen(dst) - 1 bytes from @p src.
 *
 * @param dst      NUL-terminated destination; must fit within @p dst_cap.
 * @param src      Source string.
 * @param dst_cap  Total bytes allocated for @p dst.
 * @return         Length the concatenated string would have if unlimited
 *                 (may exceed @p dst_cap - 1 when truncated).
 */
u64 kstrlcat(char *dst, const char *src, u64 dst_cap);

/**
 * @brief Check if strings are equal.
 * @param a First string.
 * @param b Second string.
 * @return true if equal.
 */
bool kstreq(const char *a, const char *b);

/**
 * @brief Find last occurrence of character in string.
 * @param s String to search.
 * @param c Character to find.
 * @return Pointer to character, or NULL if not found.
 */
char *kstrrchr(const char *s, int c);

#endif /* ALCOR2_KSTDLIB_H */
