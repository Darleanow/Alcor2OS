/**
 * @file src/mm/heap.c
 * @brief Kernel heap allocator implementation.
 *
 * First-fit allocator with block coalescing, backed by PMM pages.
 */

#include <alcor2/console.h>
#include <alcor2/heap.h>
#include <alcor2/memory_layout.h>
#include <alcor2/pmm.h>
#include <alcor2/vmm.h>
#include <alcor2/kstdlib.h>

static heap_block_t *heap_start   = NULL;
static heap_block_t *heap_end     = NULL;
static u64           heap_size    = 0;
static u64           heap_used    = 0;
static u64           heap_next_va = KERNEL_HEAP_BASE;

/**
 * @brief Expand heap by allocating and mapping new physical pages.
 * @param pages Number of 4KB pages to add.
 * @return 0 on success, negative on failure.
 */
static int heap_expand(u64 pages)
{
  if(pages == 0) {
    pages = 1;
  }

  u64 size = pages * PAGE_SIZE;

  /* Allocate physical pages (PMM returns physical address as void*) */
  void *phys_ptr = pmm_alloc_pages(pages);
  if(phys_ptr == NULL) {
    return -1;
  }

  u64 phys = (u64)phys_ptr;
  u64 virt = heap_next_va;

  /* Map each page into the heap virtual address space */
  for(u64 i = 0; i < pages; i++) {
    vmm_map(
        virt + (i * PAGE_SIZE), phys + (i * PAGE_SIZE), VMM_PRESENT | VMM_WRITE
    );
  }

  /* Update next available virtual address */
  heap_next_va += size;

  /* Initialize the new block */
  heap_block_t *block = (heap_block_t *)virt;
  block->magic        = HEAP_BLOCK_MAGIC;
  block->size         = size - HEAP_HEADER_SIZE;
  block->free         = 1;
  block->next         = NULL;
  block->prev         = heap_end;

  /* Link into the block list */
  if(heap_end != NULL) {
    heap_end->next = block;
  }
  if(heap_start == NULL) {
    heap_start = block;
  }
  heap_end = block;

  heap_size += size;

  return 0;
}

/**
 * @brief Find first free block that fits requested size.
 * @param size Minimum size needed.
 * @return Pointer to block or NULL.
 */
static heap_block_t *find_free_block(u64 size)
{
  for(heap_block_t *b = heap_start; b != NULL; b = b->next) {
    if(b->free && b->size >= size) {
      return b;
    }
  }
  return NULL;
}

/**
 * @brief Split block if remaining space is sufficient.
 * @param block Block to split.
 * @param size Size for first part.
 */
static void split_block(heap_block_t *block, u64 size)
{
  u64 remaining = block->size - size;

  if(remaining <= HEAP_HEADER_SIZE + HEAP_MIN_ALLOC) {
    return;
  }

  heap_block_t *new_block =
      (heap_block_t *)((u8 *)block + HEAP_HEADER_SIZE + size);
  new_block->magic = HEAP_BLOCK_MAGIC;
  new_block->size  = remaining - HEAP_HEADER_SIZE;
  new_block->free  = 1;
  new_block->prev  = block;
  new_block->next  = block->next;

  if(block->next != NULL) {
    block->next->prev = new_block;
  }
  if(block == heap_end) {
    heap_end = new_block;
  }

  block->size = size;
  block->next = new_block;
}

/**
 * @brief Merge block with adjacent free blocks.
 * @param block Block to coalesce.
 */
static void coalesce(heap_block_t *block)
{
  /* Merge with next block if free */
  while(block->next != NULL && block->next->free) {
    heap_block_t *next = block->next;

    block->size += HEAP_HEADER_SIZE + next->size;
    block->next = next->next;

    if(next->next != NULL) {
      next->next->prev = block;
    }
    if(next == heap_end) {
      heap_end = block;
    }
  }

  /* Merge with previous block if free */
  if(block->prev != NULL && block->prev->free) {
    heap_block_t *prev = block->prev;

    prev->size += HEAP_HEADER_SIZE + block->size;
    prev->next = block->next;

    if(block->next != NULL) {
      block->next->prev = prev;
    }
    if(block == heap_end) {
      heap_end = prev;
    }
  }
}

void heap_init(void)
{
  if(heap_expand(HEAP_INITIAL_PAGES) != 0) {
    console_print("[HEAP] Init failed!\n");
    return;
  }

  console_printf(
      "[HEAP] %d KB at 0x%x\n", (int)(heap_size / 1024),
      (unsigned int)KERNEL_HEAP_BASE_DISPLAY
  );
}

/**
 * @brief Allocate memory from kernel heap.
 * 
 * Uses first-fit algorithm to find a suitable free block. If no block is large
 * enough, expands the heap by allocating more physical pages. Blocks are automatically
 * split if significantly larger than needed.
 * 
 * @param size Number of bytes to allocate (automatically aligned to 16 bytes).
 * @return Pointer to allocated memory, or NULL on failure.
 */
void *kmalloc(u64 size)
{
  if(size == 0) {
    return NULL;
  }

  /* Align to 16 bytes */
  size = (size + 15) & ~15ULL;
  if(size < HEAP_MIN_ALLOC) {
    size = HEAP_MIN_ALLOC;
  }

  /* Find a suitable block */
  heap_block_t *block = find_free_block(size);

  /* Expand heap if needed */
  if(block == NULL) {
    u64 pages = (size + HEAP_HEADER_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if(pages < 4) {
      pages = 4;
    }

    if(heap_expand(pages) != 0) {
      return NULL;
    }

    block = find_free_block(size);
    if(block == NULL) {
      return NULL;
    }
  }

  /* Split if too large */
  split_block(block, size);

  block->free = 0;
  heap_used += block->size;

  return (void *)((u8 *)block + HEAP_HEADER_SIZE);
}

/**
 * @brief Allocate zeroed memory from kernel heap.
 * 
 * Same as kmalloc() but clears the allocated memory to zero.
 * 
 * @param size Number of bytes to allocate.
 * @return Pointer to zeroed memory, or NULL on failure.
 */
void *kzalloc(u64 size)
{
  void *ptr = kmalloc(size);
  if(ptr != NULL) {
    kzero(ptr, size);
  }
  return ptr;
}

/**
 * @brief Allocate aligned memory from kernel heap.
 * 
 * Allocates memory with the specified alignment. The alignment must be a power of 2.
 * Stores the original pointer before the aligned address for proper freeing.
 * 
 * @param size Number of bytes to allocate.
 * @param alignment Alignment in bytes (minimum 16, rounded up if smaller).
 * @return Pointer to aligned memory, or NULL on failure.
 */
// cppcheck-suppress unusedFunction
void *kmalloc_aligned(u64 size, u64 alignment)
{
  if(alignment < 16) {
    alignment = 16;
  }

  /* Allocate extra for alignment */
  void *ptr = kmalloc(size + alignment + sizeof(void *));
  if(ptr == NULL) {
    return NULL;
  }

  /* Calculate aligned address */
  u64 addr    = (u64)ptr + sizeof(void *);
  u64 aligned = (addr + alignment - 1) & ~(alignment - 1);

  /* Store original pointer */
  *((void **)(aligned - sizeof(void *))) = ptr;

  return (void *)aligned;
}

/**
 * @brief Free previously allocated memory.
 * 
 * Returns memory to the free list and attempts to coalesce with adjacent free blocks.
 * Performs validation checks for double-free and corruption.
 * 
 * @param ptr Pointer previously returned by kmalloc/kzalloc, or NULL (ignored).
 */
void kfree(void *ptr)
{
  if(ptr == NULL) {
    return;
  }

  heap_block_t *block = (heap_block_t *)((u8 *)ptr - HEAP_HEADER_SIZE);

  /* Validate */
  if(block->magic != HEAP_BLOCK_MAGIC) {
    console_print("[HEAP] Bad free: invalid magic\n");
    return;
  }

  if(block->free) {
    console_print("[HEAP] Double free detected\n");
    return;
  }

  block->free = 1;
  heap_used -= block->size;

  coalesce(block);
}

/**
 * @brief Reallocate memory to a new size.
 * 
 * If ptr is NULL, equivalent to kmalloc(new_size).
 * If new_size is 0, equivalent to kfree(ptr) and returns NULL.
 * If current block is large enough, returns the same pointer.
 * Otherwise, allocates new block, copies data, and frees old block.
 * 
 * @param ptr Pointer to previously allocated memory, or NULL.
 * @param new_size New size in bytes.
 * @return Pointer to reallocated memory, or NULL on failure.
 */
// cppcheck-suppress unusedFunction
void *krealloc(void *ptr, u64 new_size)
{
  if(ptr == NULL) {
    return kmalloc(new_size);
  }

  if(new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  const heap_block_t *block = (const heap_block_t *)((u8 *)ptr - HEAP_HEADER_SIZE);

  if(block->magic != HEAP_BLOCK_MAGIC) {
    return NULL;
  }

  /* Current block is large enough */
  if(block->size >= new_size) {
    return ptr;
  }

  /* Allocate new and copy */
  void *new_ptr = kmalloc(new_size);
  if(new_ptr == NULL) {
    return NULL;
  }

  kmemcpy(new_ptr, ptr, block->size);

  kfree(ptr);
  return new_ptr;
}

/**
 * @brief Get heap statistics.
 * 
 * Returns total heap size, used memory, and free memory.
 * Any parameter can be NULL if that statistic is not needed.
 * 
 * @param total Output pointer for total heap size in bytes (can be NULL).
 * @param used Output pointer for used heap size in bytes (can be NULL).
 * @param free_mem Output pointer for free heap size in bytes (can be NULL).
 */
// cppcheck-suppress unusedFunction
void heap_stats(heap_stats_t *stats)
{
  if(!stats)
    return;

  stats->total_bytes = heap_size;
  stats->used_bytes  = heap_used;
  stats->free_bytes  = heap_size - heap_used;
}
