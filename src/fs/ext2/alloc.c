/**
 * @file src/fs/ext2/alloc.c
 * @brief Block + inode allocation bitmap operations.
 *
 * Mirrors the on-disk layout: every block group carries its own block and
 * inode bitmaps, and the per-group descriptor caches free counts to avoid
 * scanning a full bitmap on every alloc miss. The @c *_in_group helpers do
 * the actual read-modify-write; @ref alloc_block / @ref alloc_inode wrap them
 * with the locality-biased group search.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/** @brief Bit-position width of one bitmap byte (8 bits per byte). */
#define BITMAP_BITS_PER_BYTE 8u

/** @brief Mask for "all bits set" in a single bitmap byte. */
#define BITMAP_BYTE_FULL 0xFFu

/** @brief Mask isolating the low bit-index inside one byte (@c bit @c & @c 7).
 */
#define BITMAP_BIT_INDEX_MASK 7u

/** @brief Shift count to derive a byte index from a bit index (@c bit @c >>
 * @c 3). */
#define BITMAP_BIT_INDEX_SHIFT 3u

/** @brief Set bit @p bit in the bitmap rooted at @p bitmap. */
static inline void bitmap_set(u8 *bitmap, u32 bit)
{
  bitmap[bit >> BITMAP_BIT_INDEX_SHIFT] |=
      (u8)(1u << (bit & BITMAP_BIT_INDEX_MASK));
}

/** @brief Clear bit @p bit in the bitmap rooted at @p bitmap. */
static inline void bitmap_clear(u8 *bitmap, u32 bit)
{
  bitmap[bit >> BITMAP_BIT_INDEX_SHIFT] &=
      (u8) ~(1u << (bit & BITMAP_BIT_INDEX_MASK));
}

/**
 * @brief Locate the first clear bit in a bitmap.
 *
 * Walks byte-by-byte; on a partially-clear byte, falls into the per-bit
 * loop. Stops at @p size bits so a bitmap padded out to a block boundary
 * doesn't return a phantom bit past the live range.
 *
 * @param bitmap  Bitmap buffer.
 * @param size    Number of valid bits.
 * @return Bit index, or @c (u32)-1 when every bit in range is set.
 */
static u32 bitmap_find_clear(const u8 *bitmap, u32 size)
{
  for(u32 byte = 0;
      byte < (size + BITMAP_BITS_PER_BYTE - 1u) / BITMAP_BITS_PER_BYTE;
      byte++) {
    if(bitmap[byte] == BITMAP_BYTE_FULL)
      continue;
    for(u32 bit = 0; bit < BITMAP_BITS_PER_BYTE &&
                     (byte * BITMAP_BITS_PER_BYTE + bit) < size;
        bit++) {
      if(!(bitmap[byte] & (1u << bit)))
        return byte * BITMAP_BITS_PER_BYTE + bit;
    }
  }
  return (u32)-1;
}

/**
 * @brief Try to allocate one block inside @p group.
 *
 * Read-modify-write on the group's block bitmap. The per-group descriptor's
 * @c bg_free_blocks_count short-circuits the bitmap read when the group is
 * known full; on success it's decremented along with the superblock counter.
 *
 * @param vol    Target volume.
 * @param group  Group index to try.
 * @return Block number on success, 0 on miss / I/O failure.
 */
static u32 alloc_block_in_group(ext2_volume_t *vol, u32 group)
{
  if(group >= vol->groups_count)
    return 0;

  ext2_group_desc_t *gd = &vol->groups[group];
  if(gd->bg_free_blocks_count == 0)
    return 0;

  u8 *bitmap = kmalloc(vol->block_size);
  if(!bitmap)
    return 0;

  if(vol_read_block(vol, gd->bg_block_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return 0;
  }

  u32 bit = bitmap_find_clear(bitmap, vol->blocks_per_group);
  if(bit == (u32)-1) {
    kfree(bitmap);
    return 0;
  }

  bitmap_set(bitmap, bit);

  if(vol_write_block(vol, gd->bg_block_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return 0;
  }

  kfree(bitmap);

  gd->bg_free_blocks_count--;
  vol->sb.s_free_blocks_count--;

  return group * vol->blocks_per_group + bit + vol->first_data_block;
}

u32 alloc_block(ext2_volume_t *vol, u32 preferred_group)
{
  u32 block = alloc_block_in_group(vol, preferred_group);
  if(block)
    return block;
  for(u32 g = 0; g < vol->groups_count; g++) {
    if(g == preferred_group)
      continue;
    block = alloc_block_in_group(vol, g);
    if(block)
      return block;
  }
  return 0;
}

i64 free_block(ext2_volume_t *vol, u32 block)
{
  if(block < vol->first_data_block || block >= vol->blocks_count)
    return -EINVAL;

  u32 group = (block - vol->first_data_block) / vol->blocks_per_group;
  u32 bit   = (block - vol->first_data_block) % vol->blocks_per_group;

  ext2_group_desc_t *gd = &vol->groups[group];

  u8                *bitmap = kmalloc(vol->block_size);
  if(!bitmap)
    return -ENOMEM;

  if(vol_read_block(vol, gd->bg_block_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return -EIO;
  }

  bitmap_clear(bitmap, bit);

  if(vol_write_block(vol, gd->bg_block_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return -EIO;
  }

  kfree(bitmap);

  gd->bg_free_blocks_count++;
  vol->sb.s_free_blocks_count++;
  return 0;
}

/**
 * @brief Try to allocate one inode inside @p group.
 *
 * Mirror of @ref alloc_block_in_group on the inode bitmap. When @p is_dir is
 * set, the group's @c bg_used_dirs_count is also bumped — the allocator uses
 * that hint to spread directories across groups.
 *
 * @param vol     Target volume.
 * @param group   Group index to try.
 * @param is_dir  When true, bumps the per-group dir count.
 * @return Inode number on success, 0 on miss / I/O failure.
 */
static u32 alloc_inode_in_group(ext2_volume_t *vol, u32 group, bool is_dir)
{
  if(group >= vol->groups_count)
    return 0;

  ext2_group_desc_t *gd = &vol->groups[group];
  if(gd->bg_free_inodes_count == 0)
    return 0;

  u8 *bitmap = kmalloc(vol->block_size);
  if(!bitmap)
    return 0;

  if(vol_read_block(vol, gd->bg_inode_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return 0;
  }

  u32 bit = bitmap_find_clear(bitmap, vol->inodes_per_group);
  if(bit == (u32)-1) {
    kfree(bitmap);
    return 0;
  }

  bitmap_set(bitmap, bit);

  if(vol_write_block(vol, gd->bg_inode_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return 0;
  }

  kfree(bitmap);

  gd->bg_free_inodes_count--;
  vol->sb.s_free_inodes_count--;
  if(is_dir)
    gd->bg_used_dirs_count++;

  return group * vol->inodes_per_group + bit + 1;
}

u32 alloc_inode(ext2_volume_t *vol, u32 preferred_group, bool is_dir)
{
  u32 ino = alloc_inode_in_group(vol, preferred_group, is_dir);
  if(ino)
    return ino;
  for(u32 g = 0; g < vol->groups_count; g++) {
    if(g == preferred_group)
      continue;
    ino = alloc_inode_in_group(vol, g, is_dir);
    if(ino)
      return ino;
  }
  return 0;
}

i64 free_inode(ext2_volume_t *vol, u32 ino, bool is_dir)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32                group = (ino - 1) / vol->inodes_per_group;
  u32                bit   = (ino - 1) % vol->inodes_per_group;
  ext2_group_desc_t *gd    = &vol->groups[group];

  u8                *bitmap = kmalloc(vol->block_size);
  if(!bitmap)
    return -ENOMEM;

  if(vol_read_block(vol, gd->bg_inode_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return -EIO;
  }

  bitmap_clear(bitmap, bit);

  if(vol_write_block(vol, gd->bg_inode_bitmap, bitmap) < 0) {
    kfree(bitmap);
    return -EIO;
  }

  kfree(bitmap);

  gd->bg_free_inodes_count++;
  vol->sb.s_free_inodes_count++;
  if(is_dir && gd->bg_used_dirs_count > 0)
    gd->bg_used_dirs_count--;
  return 0;
}
