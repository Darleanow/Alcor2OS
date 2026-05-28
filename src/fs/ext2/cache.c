/**
 * @file src/fs/ext2/cache.c
 * @brief Single-block scratch pool for one-off ext2 metadata reads.
 *
 * Not a block-content cache — the underlying blockdev (ATA driver) owns its
 * own sector cache. This pool just sidesteps @c kmalloc churn for the burst
 * of one-block metadata reads that every fs operation issues (superblock,
 * group descriptors, inode table, bitmap, indirect block).
 */

#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief One slot of the scratch pool — the block payload plus a busy flag.
 *
 * Stored as a fixed-size array (not a free-list) because @ref cache_put_block
 * has to map an arbitrary @c u8* back to its slot; pointer-equality against
 * the @c data field is the cheapest match for that.
 */
typedef struct
{
  u8 data[EXT2_MAX_BLOCK_SIZE]; /**< Block payload, sized to the largest
                                    block we support. */
  bool in_use;                  /**< true while a caller holds @c data. */
} block_pool_entry_t;

/**
 * @brief Static pool — sized at @ref EXT2_BLOCK_CACHE_SIZE entries to overlap
 * the deepest indirect-block walk plus a few concurrent metadata reads.
 */
static block_pool_entry_t g_block_pool[EXT2_BLOCK_CACHE_SIZE];

u8                       *cache_get_block(u32 size)
{
  if(size > EXT2_MAX_BLOCK_SIZE)
    return kmalloc(size);

  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++) {
    if(!g_block_pool[i].in_use) {
      g_block_pool[i].in_use = true;
      return g_block_pool[i].data;
    }
  }

  /* Pool exhausted — fall back to kmalloc; cache_put_block detects via
   * pointer identity and routes to kfree. */
  return kmalloc(size);
}

void cache_put_block(u8 *buf)
{
  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++) {
    if(buf == g_block_pool[i].data) {
      g_block_pool[i].in_use = false;
      return;
    }
  }

  /* Pointer not in pool — must have come from the kmalloc fallback. */
  kfree(buf);
}
