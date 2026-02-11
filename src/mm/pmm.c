/**
 * @file src/mm/pmm.c
 * @brief Physical memory manager using bitmap allocator.
 */

#include <alcor2/limine.h>
#include <alcor2/memory_layout.h>
#include <alcor2/pmm.h>
#include <alcor2/types.h>

#define BITS_PER_ENTRY 64

static u64 *bitmap;
static u64  bitmap_size;
static u64  total_pages;
static u64  free_pages;
static u64  hhdm;

/**
 * @brief Mark page as allocated in bitmap.
 * @param page Page number.
 */
static inline void bitmap_set(u64 page)
{
  bitmap[page / BITS_PER_ENTRY] |= (1ULL << (page % BITS_PER_ENTRY));
}

/**
 * @brief Mark page as free in bitmap.
 * @param page Page number.
 */
static inline void bitmap_clear(u64 page)
{
  bitmap[page / BITS_PER_ENTRY] &= ~(1ULL << (page % BITS_PER_ENTRY));
}

/**
 * @brief Test if page is allocated.
 * @param page Page number.
 * @return true if allocated.
 */
static inline bool bitmap_test(u64 page)
{
  return bitmap[page / BITS_PER_ENTRY] & (1ULL << (page % BITS_PER_ENTRY));
}

/**
 * @brief Initialize the physical memory manager.
 *
 * Parses the memory map to build a bitmap of free/used pages
 * and reserves kernel memory regions.
 *
 * @param memmap Limine memory map response.
 * @param hhdm_offset Higher-half direct map offset.
 */
void pmm_init(struct limine_memmap_response *memmap, u64 hhdm_offset)
{
  hhdm = hhdm_offset;

  u64 highest_addr = 0;
  for(u64 i = 0; i < memmap->entry_count; i++) {
    const struct limine_memmap_entry *e   = memmap->entries[i];
    u64                               top = e->base + e->length;
    if(e->type == LIMINE_MEMMAP_USABLE && top > highest_addr) {
      highest_addr = top;
    }
  }

  total_pages = highest_addr / PAGE_SIZE;
  bitmap_size =
      (total_pages + BITS_PER_ENTRY - 1) / BITS_PER_ENTRY * sizeof(u64);

  for(u64 i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *e = memmap->entries[i];
    if(e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmap_size) {
      bitmap = (u64 *)(e->base + hhdm);
      e->base += bitmap_size;
      e->length -= bitmap_size;
      break;
    }
  }

  for(u64 i = 0; i < bitmap_size / sizeof(u64); i++) {
    bitmap[i] = ALL_BITS_SET;
  }
  free_pages = 0;

  for(u64 i = 0; i < memmap->entry_count; i++) {
    const struct limine_memmap_entry *e = memmap->entries[i];
    if(e->type == LIMINE_MEMMAP_USABLE) {
      u64 start = (e->base + PAGE_SIZE - 1) / PAGE_SIZE;
      u64 end   = (e->base + e->length) / PAGE_SIZE;
      for(u64 p = start; p < end; p++) {
        bitmap_clear(p);
        free_pages++;
      }
    }
  }
}

/**
 * @brief Allocate a single 4KB physical page.
 *
 * Searches the bitmap for the first free page, marks it as allocated,
 * and returns its physical address.
 *
 * @return Physical address of the allocated page, or NULL if out of memory.
 */
void *pmm_alloc(void)
{
  for(u64 i = 0; i < bitmap_size / sizeof(u64); i++) {
    if(bitmap[i] != ALL_BITS_SET) {
      for(int b = 0; b < BITS_PER_ENTRY; b++) {
        u64 page = i * BITS_PER_ENTRY + b;
        if(!bitmap_test(page)) {
          bitmap_set(page);
          free_pages--;
          return (void *)(page * PAGE_SIZE);
        }
      }
    }
  }
  return 0;
}

/**
 * @brief Allocate multiple contiguous physical pages.
 *
 * Searches for a contiguous region of free pages, marks them as allocated,
 * and returns the physical address of the first page.
 *
 * @param count Number of pages to allocate.
 * @return Physical address of the first page, or NULL if not enough contiguous
 * memory.
 */
void *pmm_alloc_pages(usize count)
{
  u64 consecutive = 0;
  u64 start_page  = 0;

  for(u64 p = 0; p < total_pages; p++) {
    if(!bitmap_test(p)) {
      if(consecutive == 0)
        start_page = p;
      consecutive++;
      if(consecutive == count) {
        for(u64 i = start_page; i < start_page + count; i++) {
          bitmap_set(i);
        }
        free_pages -= count;
        return (void *)(start_page * PAGE_SIZE);
      }
    } else {
      consecutive = 0;
    }
  }
  return 0;
}

/**
 * @brief Free a single physical page.
 *
 * Marks the page as free in the bitmap, making it available for reallocation.
 *
 * @param addr Physical address of the page to free.
 */
void pmm_free(void *addr)
{
  u64 page = (u64)addr / PAGE_SIZE;
  if(bitmap_test(page)) {
    bitmap_clear(page);
    free_pages++;
  }
}

/**
 * @brief Free multiple contiguous physical pages.
 *
 * Marks a range of pages as free in the bitmap.
 *
 * @param addr Physical address of the first page.
 * @param count Number of pages to free.
 */
// cppcheck-suppress unusedFunction
void pmm_free_pages(void *addr, usize count)
{
  u64 page = (u64)addr / PAGE_SIZE;
  for(usize i = 0; i < count; i++) {
    if(bitmap_test(page + i)) {
      bitmap_clear(page + i);
      free_pages++;
    }
  }
}

/**
 * @brief Get total physical memory size.
 * @return Total memory in bytes.
 */
u64 pmm_get_total(void)
{
  return total_pages * PAGE_SIZE;
}

/**
 * @brief Get free physical memory size.
 * @return Free memory in bytes.
 */
u64 pmm_get_free(void)
{
  return free_pages * PAGE_SIZE;
}
