/**
 * @file include/alcor2/heap.h
 * @brief Kernel heap allocator.
 *
 * Simple free-list based dynamic memory allocator for the kernel.
 */

#ifndef ALCOR2_HEAP_H
#define ALCOR2_HEAP_H

#include <alcor2/types.h>

/** @brief Initial heap size in 4K pages. */
#define HEAP_INITIAL_PAGES 16

/** @brief Magic number for block validation. */
#define HEAP_BLOCK_MAGIC 0xDEADBEEF

/**
 * @brief Heap block header for free list.
 */
typedef struct heap_block
{
  u32                magic;
  u32                size;
  u8                 free;
  u8                 reserved[7];
  struct heap_block *next;
  struct heap_block *prev;
} PACKED heap_block_t;

/** @brief Size of the block header structure. */
#define HEAP_HEADER_SIZE sizeof(heap_block_t)

/** @brief Minimum allocation size in bytes. */
#define HEAP_MIN_ALLOC 32

/**
 * @brief Initialize the kernel heap.
 */
void heap_init(void);

/**
 * @brief Allocate memory.
 * @param size Bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
void *kmalloc(u64 size);

/**
 * @brief Allocate zeroed memory.
 * @param size Bytes to allocate.
 * @return Pointer to zeroed memory, or NULL on failure.
 */
void *kzalloc(u64 size);

/**
 * @brief Allocate aligned memory.
 * @param size Bytes to allocate.
 * @param alignment Alignment in bytes (power of 2).
 * @return Pointer to aligned memory, or NULL on failure.
 */
void *kmalloc_aligned(u64 size, u64 alignment);

/**
 * @brief Free memory.
 * @param ptr Pointer previously returned by kmalloc/kzalloc.
 */
void kfree(void *ptr);

/**
 * @brief Reallocate memory.
 * @param ptr Existing pointer (or NULL).
 * @param new_size New size in bytes.
 * @return Pointer to reallocated memory, or NULL on failure.
 */
void *krealloc(void *ptr, u64 new_size);

/**
 * @brief Heap statistics.
 */
typedef struct
{
  u64 total_bytes;
  u64 used_bytes;
  u64 free_bytes;
} heap_stats_t;

/**
 * @brief Get heap statistics.
 * @param stats Pointer to stats structure to fill.
 */
void heap_stats(heap_stats_t *stats);

#endif
