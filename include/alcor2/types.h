/**
 * @file include/alcor2/types.h
 * @brief Common type definitions and macros.
 *
 * Provides fixed-width integer types, boolean, size types,
 * and compiler attribute macros.
 */

#ifndef ALCOR2_TYPES_H
#define ALCOR2_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @name Fixed-width unsigned integer types
 * @{ */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
/** @} */

/** @name Fixed-width signed integer types
 * @{ */
typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
/** @} */

/** @brief Size type. */
typedef size_t usize;

/** @brief Pack structure (no padding). */
#define PACKED __attribute__((packed))

/** @brief Align structure to n-byte boundary. */
#define ALIGNED(n) __attribute__((aligned(n)))

/** @brief Mark as unused (suppress warnings). */
#define UNUSED __attribute__((unused))

/** @brief Function never returns. */
#define NORETURN __attribute__((noreturn))

/** @brief Place in specific ELF section. */
#define SECTION(s) __attribute__((section(s)))

/** @brief Force linker to keep symbol. */
#define USED __attribute__((used))

/** @brief Check pointer is not NULL. */
#define NULL_CHECK(ptr) ((ptr) != NULL)

#endif
