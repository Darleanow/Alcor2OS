/**
 * @file include/uapi/alcor2/types.h
 * @brief Shared fixed-width integer typedefs for the kernel/userland ABI.
 *
 * UAPI structs use these (@c u8…@c u64, @c i8…@c i64, @c usize) so the
 * shorthand stays consistent across the tree. Kernel-private macros
 * (@c PACKED, @c NORETURN, …) live in @c <alcor2/types.h>, which is a
 * superset that pulls in this header.
 *
 * Userland gets only @c <stdint.h> / @c <stddef.h> / @c <stdbool.h> here —
 * no other alcor2 dependency.
 */

#ifndef ALCOR2_UAPI_TYPES_H
#define ALCOR2_UAPI_TYPES_H

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

#endif
