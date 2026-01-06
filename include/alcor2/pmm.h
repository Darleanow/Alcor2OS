/**
 * @file include/alcor2/pmm.h
 * @brief Physical memory manager (page allocator).
 *
 * Bitmap-based allocator for 4K physical pages.
 */

#ifndef ALCOR2_PMM_H
#define ALCOR2_PMM_H

#include <alcor2/limine.h>
#include <alcor2/types.h>

/** @brief Size of a physical page. */
#define PAGE_SIZE 4096

/**
 * @brief Initialize the physical memory manager.
 * @param memmap Limine memory map response.
 * @param hhdm_offset Higher-half direct map offset.
 */
void pmm_init(struct limine_memmap_response *memmap, u64 hhdm_offset);

/**
 * @brief Allocate a single 4K page.
 * @return Physical address of the page, or NULL on failure.
 */
void *pmm_alloc(void);

/**
 * @brief Allocate multiple contiguous pages.
 * @param count Number of pages.
 * @return Physical address of the first page, or NULL on failure.
 */
void *pmm_alloc_pages(usize count);

/**
 * @brief Free a single page.
 * @param addr Physical address of the page.
 */
void pmm_free(void *addr);

/**
 * @brief Free multiple contiguous pages.
 * @param addr Physical address of the first page.
 * @param count Number of pages.
 */
void pmm_free_pages(void *addr, usize count);

/**
 * @brief Get total physical memory in bytes.
 * @return Total memory size.
 */
u64 pmm_get_total(void);

/**
 * @brief Get free physical memory in bytes.
 * @return Free memory size.
 */
u64 pmm_get_free(void);

#endif
