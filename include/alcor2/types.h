/**
 * @file include/alcor2/types.h
 * @brief Kernel-superset of @c <uapi/alcor2/types.h>.
 *
 * Re-exports the UAPI integer typedefs and adds kernel-side compiler
 * attribute macros (@c PACKED, @c ALIGNED, @c NORETURN, @c SECTION,
 * @c USED). UAPI headers include the UAPI version directly so userland
 * never sees the macros.
 */

#ifndef ALCOR2_TYPES_H
#define ALCOR2_TYPES_H

#include <uapi/alcor2/types.h>

/** @brief Pack structure (no padding). */
#define PACKED __attribute__((packed))

/** @brief Align structure to n-byte boundary. */
#define ALIGNED(n) __attribute__((aligned(n)))

/** @brief Function never returns. */
#define NORETURN __attribute__((noreturn))

/** @brief Place in specific ELF section. */
#define SECTION(s) __attribute__((section(s)))

/** @brief Force linker to keep symbol. */
#define USED __attribute__((used))

#endif
