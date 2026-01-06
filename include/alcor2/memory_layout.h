/**
 * @file include/alcor2/memory_layout.h
 * @brief Virtual memory layout constants for x86_64.
 *
 * Defines the memory map for both kernel and user space.
 * All magic addresses should be defined here.
 */

#ifndef ALCOR2_MEMORY_LAYOUT_H
#define ALCOR2_MEMORY_LAYOUT_H

#include <alcor2/types.h>

/** @name Kernel Virtual Address Space
 * Higher-half kernel mapping (0xFFFF800000000000 and above)
 * @{ */

/** @brief Start of kernel space */
#define KERNEL_SPACE_START 0xFFFF800000000000ULL

/** @brief Kernel heap base address */
#define KERNEL_HEAP_BASE 0xFFFFFFFF90000000ULL

/** @brief Kernel heap initial address for debug display */
#define KERNEL_HEAP_BASE_DISPLAY 0x90000000

/** @} */

/** @name User Virtual Address Space
 * User space occupies 0x0000000000000000 to 0x00007FFFFFFFFFFF
 * @{ */

/** @brief User space start (after NULL page) */
#define USER_SPACE_START 0x0000000000400000ULL

/** @brief User space end (canonical address limit) */
#define USER_SPACE_END 0x00007FFFFFFFF000ULL

/** @brief User stack top (grows downward) */
#define USER_STACK_TOP 0x00007FFF00000000ULL

/** @brief User stack size (64KB per process) */
#define USER_STACK_SIZE (64 * 1024)

/** @brief User stack base calculation */
#define USER_STACK_BASE (USER_STACK_TOP - USER_STACK_SIZE)

/** @brief User heap start for mmap (1GB) */
#define USER_HEAP_START 0x0000000040000000ULL

/** @} */

/** @name Page Table Manipulation
 * @{ */

/** @brief Page frame number mask (bits 12-51) */
#define PAGE_FRAME_MASK 0x000FFFFFFFFFF000ULL

/** @brief Page offset mask (bits 0-11) */
#define PAGE_OFFSET_MASK 0x0000000000000FFFULL

/** @brief Page table index mask (9 bits) */
#define PAGE_TABLE_INDEX_MASK 0x1FF

/** @brief Alignment mask for 8-byte alignment */
#define ALIGN_8_MASK (~0x7ULL)

/** @brief Alignment mask for 16-byte alignment */
#define ALIGN_16_MASK (~0xFULL)

/** @} */

/** @name Special Values
 * @{ */

/** @brief Sentinel value for uninitialized ELF base */
#define ELF_BASE_SENTINEL 0xFFFFFFFFFFFFFFFFULL

/** @brief All bits set (used for bitmap initialization) */
#define ALL_BITS_SET 0xFFFFFFFFFFFFFFFFULL

/** @} */

#endif /* ALCOR2_MEMORY_LAYOUT_H */
