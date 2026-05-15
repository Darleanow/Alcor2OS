/**
 * @file src/fs/ext2.c
 * @brief ext2 filesystem driver.
 *
 * Second Extended Filesystem implementation for Alcor2.
 *
 * Features:
 * - Read/write support
 * - Direct, indirect, double-indirect, and triple-indirect block addressing
 * - Directory operations (create, remove, traverse)
 * - File operations (open, read, write, seek, truncate)
 * - Bitmap-based allocation for blocks and inodes
 *
 * Limitations:
 * - No extended attributes
 * - No journal support
 */

#include <alcor2/drivers/ata.h>
#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>

/** @brief Maximum mounted ext2 volumes. */
#define EXT2_MAX_VOLUMES 4

/** @brief Maximum concurrent open files. */
#define EXT2_MAX_FILES 256

/** @brief Block buffer cache size (pool for single-block I/O). */
#define EXT2_BLOCK_CACHE_SIZE 8

/** @brief Maximum supported block size (for cache). */
#define EXT2_MAX_BLOCK_SIZE 4096

/** @brief Direct-mapped disk block cache (512 x 4 KB = 2 MB). */
#define EXT2_DSK_CACHE_SLOTS 512

typedef struct
{
  const ext2_volume_t *vol; /**< NULL = slot unused */
  u32                  block_num;
  u8                   data[EXT2_MAX_BLOCK_SIZE];
} dsk_cache_slot_t;

static dsk_cache_slot_t g_dsk_cache[EXT2_DSK_CACHE_SLOTS];

static inline u32       dsk_cache_idx(const ext2_volume_t *vol, u32 bn)
{
  return (u32)(((uintptr_t)vol >> 4) ^ (u64)bn * 2654435761ULL) &
         (EXT2_DSK_CACHE_SLOTS - 1);
}

static void dsk_cache_insert(
    const ext2_volume_t *vol, u32 bn, const void *data, u32 size
)
{
  u32 idx                    = dsk_cache_idx(vol, bn);
  g_dsk_cache[idx].vol       = vol;
  g_dsk_cache[idx].block_num = bn;
  if(size > EXT2_MAX_BLOCK_SIZE)
    size = EXT2_MAX_BLOCK_SIZE;
  kmemcpy(g_dsk_cache[idx].data, data, size);
}

static const u8 *dsk_cache_lookup(const ext2_volume_t *vol, u32 bn)
{
  u32 idx = dsk_cache_idx(vol, bn);
  if(g_dsk_cache[idx].vol == vol && g_dsk_cache[idx].block_num == bn)
    return g_dsk_cache[idx].data;
  return NULL;
}

/** @brief Max contiguous blocks coalesced in a single read (16 x 4 KB). */
#define EXT2_READ_RUN_MAX 16

/** @brief Pool of mounted volumes. */
static ext2_volume_t g_volumes[EXT2_MAX_VOLUMES];

/** @brief Pool of open file handles. */
static ext2_file_t g_files[EXT2_MAX_FILES];

/** @brief Forward declaration for filesystem type descriptor. */
static const fs_type_t g_ext2_fstype;

/**
 * @brief Block buffer cache entry.
 *
 * Simple stack-based pool of pre-allocated block buffers to avoid repeated
 * kmalloc/kfree calls. Thread-safety note: not thread-safe, kernel is
 * single-threaded for now.
 */
typedef struct
{
  u8   data[EXT2_MAX_BLOCK_SIZE]; /**< Block data buffer */
  bool in_use;                    /**< Buffer is currently in use */
} block_cache_entry_t;

static block_cache_entry_t g_block_cache[EXT2_BLOCK_CACHE_SIZE];

/**
 * @brief Acquire a block buffer from cache.
 * @param size Required buffer size (must be <= EXT2_MAX_BLOCK_SIZE).
 * @return Pointer to buffer, or NULL if cache exhausted (falls back to
 * kmalloc).
 */
static u8 *cache_get_block(u32 size)
{
  if(size > EXT2_MAX_BLOCK_SIZE)
    return kmalloc(size);

  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++) {
    if(!g_block_cache[i].in_use) {
      g_block_cache[i].in_use = true;
      return g_block_cache[i].data;
    }
  }

  /* Cache exhausted, fall back to kmalloc */
  return kmalloc(size);
}

/**
 * @brief Release a block buffer back to cache.
 * @param buf Buffer to release.
 */
static void cache_put_block(u8 *buf)
{
  /* Check if buffer is from cache */
  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++) {
    if(buf == g_block_cache[i].data) {
      g_block_cache[i].in_use = false;
      return;
    }
  }

  /* Not from cache, was dynamically allocated */
  kfree(buf);
}

/**
 * @brief Read sectors from volume.
 * @param vol    Volume descriptor.
 * @param sector Sector offset relative to partition start.
 * @param count  Number of 512-byte sectors to read.
 * @param buf    Destination buffer.
 * @return Bytes read on success, negative errno on error.
 */
static inline i64
    vol_read_sectors(const ext2_volume_t *vol, u32 sector, u32 count, void *buf)
{
  return ata_read(vol->drive, vol->partition_lba + sector, count, buf);
}

/**
 * @brief Write sectors to volume.
 * @param vol    Volume descriptor.
 * @param sector Sector offset relative to partition start.
 * @param count  Number of 512-byte sectors to write.
 * @param buf    Source buffer.
 * @return Bytes written on success, negative errno on error.
 */
static inline i64 vol_write_sectors(
    const ext2_volume_t *vol, u32 sector, u32 count, const void *buf
)
{
  return ata_write(vol->drive, vol->partition_lba + sector, count, buf);
}

/**
 * @brief Read a filesystem block.
 * @param vol   Volume descriptor.
 * @param block Block number (0 = superblock area).
 * @param buf   Destination buffer (vol->block_size bytes).
 * @return 0 on success, negative errno on error.
 */
static i64 vol_read_block(const ext2_volume_t *vol, u32 block, void *buf)
{
  const u8 *cached = dsk_cache_lookup(vol, block);
  if(cached) {
    kmemcpy(buf, cached, vol->block_size);
    return 0;
  }

  const u32 sectors_per_block = vol->block_size / EXT2_SECTOR_SIZE;
  const u32 sector            = block * sectors_per_block;

  i64       ret = vol_read_sectors(vol, sector, sectors_per_block, buf);
  if(ret >= 0)
    dsk_cache_insert(vol, block, buf, vol->block_size);
  return ret;
}

/**
 * @brief Write a filesystem block.
 * @param vol   Volume descriptor.
 * @param block Block number.
 * @param buf   Source buffer (vol->block_size bytes).
 * @return 0 on success, negative errno on error.
 */
static i64 vol_write_block(const ext2_volume_t *vol, u32 block, const void *buf)
{
  const u32 sectors_per_block = vol->block_size / EXT2_SECTOR_SIZE;
  const u32 sector            = block * sectors_per_block;

  i64       ret = vol_write_sectors(vol, sector, sectors_per_block, buf);
  if(ret >= 0) {
    /* Invalidate cache for this block */
    u32 idx              = dsk_cache_idx(vol, block);
    g_dsk_cache[idx].vol = NULL;
  }
  return ret;
}

/**
 * @brief Write superblock back to disk.
 * @param vol Volume with modified superblock.
 * @return 0 on success, negative on error.
 */
static i64 write_superblock(ext2_volume_t *vol)
{
  u8 buf[EXT2_MIN_BLOCK_SIZE];

  /* Read the block containing superblock */
  if(vol_read_sectors(vol, 2, 2, buf) < 0) {
    return -EIO;
  }

  /* Copy superblock to buffer */
  kmemcpy(buf, &vol->sb, sizeof(ext2_superblock_t));

  /* Write back */
  if(vol_write_sectors(vol, 2, 2, buf) < 0) {
    return -EIO;
  }

  return 0;
}

/**
 * @brief Write group descriptor table back to disk.
 * @param vol Volume.
 * @return 0 on success, negative on error.
 */
static i64 write_group_descriptors(ext2_volume_t *vol)
{
  u32 gdt_block     = vol->first_data_block + 1;
  u32 gdt_size      = vol->groups_count * sizeof(ext2_group_desc_t);
  u32 blocks_needed = (gdt_size + vol->block_size - 1) / vol->block_size;

  u8 *buf = kmalloc(vol->block_size);
  if(!buf)
    return -ENOMEM;

  u32 groups_per_block = vol->block_size / sizeof(ext2_group_desc_t);

  for(u32 b = 0; b < blocks_needed; b++) {
    kzero(buf, vol->block_size);
    u32 start_group = b * groups_per_block;
    u32 count       = vol->groups_count - start_group;
    if(count > groups_per_block)
      count = groups_per_block;

    kmemcpy(buf, &vol->groups[start_group], count * sizeof(ext2_group_desc_t));

    if(vol_write_block(vol, gdt_block + b, buf) < 0) {
      kfree(buf);
      return -EIO;
    }
  }

  kfree(buf);
  return 0;
}

/** @brief Set a bit in a bitmap. */
static inline void bitmap_set(u8 *bitmap, u32 bit)
{
  bitmap[bit >> 3] |= (u8)(1 << (bit & 7));
}

/** @brief Clear a bit in a bitmap. */
static inline void bitmap_clear(u8 *bitmap, u32 bit)
{
  bitmap[bit >> 3] &= (u8) ~(1 << (bit & 7));
}

/** @brief Test if a bit is set in a bitmap. */
static inline bool bitmap_test(const u8 *bitmap, u32 bit)
    __attribute__((unused));
static inline bool bitmap_test(const u8 *bitmap, u32 bit)
{
  return (bitmap[bit >> 3] & (1 << (bit & 7))) != 0;
}

/**
 * @brief Find first clear bit in bitmap.
 * @param bitmap Bitmap buffer.
 * @param size   Number of bits to scan.
 * @return Bit index if found, (u32)-1 if all bits are set.
 */
static u32 bitmap_find_clear(const u8 *bitmap, u32 size)
{
  for(u32 byte = 0; byte < (size + 7) / 8; byte++) {
    if(bitmap[byte] != 0xFF) {
      /* Found a byte with a clear bit */
      for(u32 bit = 0; bit < 8 && (byte * 8 + bit) < size; bit++) {
        if(!(bitmap[byte] & (1 << bit))) {
          return byte * 8 + bit;
        }
      }
    }
  }
  return (u32)-1;
}

/**
 * @brief Flush volume metadata to disk.
 *
 * Writes superblock and group descriptor table.
 *
 * @param vol Volume to flush.
 * @return 0 on success, negative errno on error.
 */
static i64 flush_metadata(ext2_volume_t *vol)
{
  i64 ret = write_superblock(vol);
  if(ret < 0)
    return ret;

  return write_group_descriptors(vol);
}

/**
 * @brief Allocate a block from a specific group.
 * @param vol Volume.
 * @param group Group number.
 * @return Block number, or 0 on failure.
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

  u32 block = group * vol->blocks_per_group + bit + vol->first_data_block;
  return block;
}

/**
 * @brief Allocate a block.
 * @param vol Volume.
 * @param preferred_group Preferred group (hint).
 * @return Block number, or 0 on failure.
 */
static u32 alloc_block(ext2_volume_t *vol, u32 preferred_group)
{
  /* Try preferred group first */
  u32 block = alloc_block_in_group(vol, preferred_group);
  if(block)
    return block;

  /* Search all groups */
  for(u32 g = 0; g < vol->groups_count; g++) {
    if(g == preferred_group)
      continue;
    block = alloc_block_in_group(vol, g);
    if(block)
      return block;
  }

  return 0;
}

/**
 * @brief Free a block.
 * @param vol Volume.
 * @param block Block number.
 * @return 0 on success, negative on error.
 */
static i64 free_block(ext2_volume_t *vol, u32 block)
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
 * @brief Allocate an inode from a specific group.
 * @param vol Volume.
 * @param group Group number.
 * @param is_dir True if allocating for a directory.
 * @return Inode number, or 0 on failure.
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

  u32 inode = group * vol->inodes_per_group + bit + 1;
  return inode;
}

/**
 * @brief Allocate an inode.
 * @param vol Volume.
 * @param preferred_group Preferred group.
 * @param is_dir True if allocating for a directory.
 * @return Inode number, or 0 on failure.
 */
static u32 alloc_inode(ext2_volume_t *vol, u32 preferred_group, bool is_dir)
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

/**
 * @brief Free an inode.
 * @param vol Volume.
 * @param ino Inode number.
 * @param is_dir True if this was a directory.
 * @return 0 on success, negative on error.
 */
static i64 free_inode(ext2_volume_t *vol, u32 ino, bool is_dir)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32                group = (ino - 1) / vol->inodes_per_group;
  u32                bit   = (ino - 1) % vol->inodes_per_group;

  ext2_group_desc_t *gd = &vol->groups[group];

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

/**
 * @brief Read an inode from disk.
 * @param vol Volume.
 * @param ino Inode number.
 * @param inode Output inode structure.
 * @return 0 on success, negative on error.
 */
static i64 read_inode(const ext2_volume_t *vol, u32 ino, ext2_inode_t *inode)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32 group       = (ino - 1) / vol->inodes_per_group;
  u32 index       = (ino - 1) % vol->inodes_per_group;
  u32 inode_table = vol->groups[group].bg_inode_table;

  u32 inodes_per_block = vol->block_size / vol->inode_size;
  u32 block            = inode_table + (index / inodes_per_block);
  u32 offset           = (index % inodes_per_block) * vol->inode_size;

  u8 *buf = kmalloc(vol->block_size);
  if(!buf)
    return -ENOMEM;

  if(vol_read_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kmemcpy(inode, buf + offset, sizeof(ext2_inode_t));
  kfree(buf);

  return 0;
}

/**
 * @brief Write an inode to disk.
 * @param vol Volume.
 * @param ino Inode number.
 * @param inode Inode structure.
 * @return 0 on success, negative on error.
 */
static i64
    write_inode(const ext2_volume_t *vol, u32 ino, const ext2_inode_t *inode)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32 group       = (ino - 1) / vol->inodes_per_group;
  u32 index       = (ino - 1) % vol->inodes_per_group;
  u32 inode_table = vol->groups[group].bg_inode_table;

  u32 inodes_per_block = vol->block_size / vol->inode_size;
  u32 block            = inode_table + (index / inodes_per_block);
  u32 offset           = (index % inodes_per_block) * vol->inode_size;

  u8 *buf = kmalloc(vol->block_size);
  if(!buf)
    return -ENOMEM;

  if(vol_read_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kmemcpy(buf + offset, inode, sizeof(ext2_inode_t));

  if(vol_write_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kfree(buf);
  return 0;
}

/**
 * @brief Get block number for a given file block index.
 * @param vol Volume.
 * @param inode Inode.
 * @param file_block File block index.
 * @return Block number, or 0 if not allocated.
 */
static u32 get_block_num(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block
)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Direct blocks */
  if(file_block < EXT2_NDIR_BLOCKS) {
    return inode->i_block[file_block];
  }

  file_block -= EXT2_NDIR_BLOCKS;

  /* Single indirect */
  if(file_block < ptrs_per_block) {
    if(inode->i_block[EXT2_IND_BLOCK] == 0)
      return 0;

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 result = ind_block[file_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block;

  /* Double indirect */
  if(file_block < ptrs_per_block * ptrs_per_block) {
    if(inode->i_block[EXT2_DIND_BLOCK] == 0)
      return 0;

    u32 *dind_block = kmalloc(vol->block_size);
    if(!dind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) < 0) {
      kfree(dind_block);
      return 0;
    }

    u32 ind_idx       = file_block / ptrs_per_block;
    u32 ind_block_num = dind_block[ind_idx];
    kfree(dind_block);

    if(ind_block_num == 0)
      return 0;

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 result = ind_block[file_block % ptrs_per_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block * ptrs_per_block;

  /* Triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK] == 0)
    return 0;

  u32 *tind_block = kmalloc(vol->block_size);
  if(!tind_block)
    return 0;

  if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) < 0) {
    kfree(tind_block);
    return 0;
  }

  u32 dind_idx       = file_block / (ptrs_per_block * ptrs_per_block);
  u32 dind_block_num = tind_block[dind_idx];
  kfree(tind_block);

  if(dind_block_num == 0)
    return 0;

  u32 *dind_block = kmalloc(vol->block_size);
  if(!dind_block)
    return 0;

  if(vol_read_block(vol, dind_block_num, dind_block) < 0) {
    kfree(dind_block);
    return 0;
  }

  u32 remaining     = file_block % (ptrs_per_block * ptrs_per_block);
  u32 ind_idx       = remaining / ptrs_per_block;
  u32 ind_block_num = dind_block[ind_idx];
  kfree(dind_block);

  if(ind_block_num == 0)
    return 0;

  u32 *ind_block = kmalloc(vol->block_size);
  if(!ind_block)
    return 0;

  if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
    kfree(ind_block);
    return 0;
  }

  u32 result = ind_block[remaining % ptrs_per_block];
  kfree(ind_block);
  return result;
}

/**
 * @brief Allocate and set a block for a given file block index.
 * @param vol Volume.
 * @param inode Inode (will be modified).
 * @param file_block File block index.
 * @param preferred_group Preferred block group for allocation.
 * @return Block number, or 0 on failure.
 */
static u32 alloc_file_block(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 file_block, u32 preferred_group
)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Direct blocks */
  if(file_block < EXT2_NDIR_BLOCKS) {
    if(inode->i_block[file_block] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0)
        return 0;
      inode->i_block[file_block] = block;
      inode->i_blocks += vol->block_size / 512;

      /* Zero the new block */
      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }
    return inode->i_block[file_block];
  }

  file_block -= EXT2_NDIR_BLOCKS;

  /* Single indirect */
  if(file_block < ptrs_per_block) {
    /* Allocate indirect block if needed */
    if(inode->i_block[EXT2_IND_BLOCK] == 0) {
      u32 ind = alloc_block(vol, preferred_group);
      if(ind == 0)
        return 0;
      inode->i_block[EXT2_IND_BLOCK] = ind;
      inode->i_blocks += vol->block_size / 512;

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, ind, zero);
        kfree(zero);
      }
    }

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    if(ind_block[file_block] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0) {
        kfree(ind_block);
        return 0;
      }
      ind_block[file_block] = block;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }

    u32 result = ind_block[file_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block;

  /* Double indirect */
  if(file_block < ptrs_per_block * ptrs_per_block) {
    /* Allocate double indirect block if needed */
    if(inode->i_block[EXT2_DIND_BLOCK] == 0) {
      u32 dind = alloc_block(vol, preferred_group);
      if(dind == 0)
        return 0;
      inode->i_block[EXT2_DIND_BLOCK] = dind;
      inode->i_blocks += vol->block_size / 512;

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, dind, zero);
        kfree(zero);
      }
    }

    u32 *dind_block = kmalloc(vol->block_size);
    if(!dind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) < 0) {
      kfree(dind_block);
      return 0;
    }

    u32 ind_idx = file_block / ptrs_per_block;

    /* Allocate indirect block if needed */
    if(dind_block[ind_idx] == 0) {
      u32 ind = alloc_block(vol, preferred_group);
      if(ind == 0) {
        kfree(dind_block);
        return 0;
      }
      dind_block[ind_idx] = ind;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, ind, zero);
        kfree(zero);
      }
    }

    u32 ind_block_num = dind_block[ind_idx];
    kfree(dind_block);

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 ind_offset = file_block % ptrs_per_block;
    if(ind_block[ind_offset] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0) {
        kfree(ind_block);
        return 0;
      }
      ind_block[ind_offset] = block;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, ind_block_num, ind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }

    u32 result = ind_block[ind_offset];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block * ptrs_per_block;

  /* Triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK] == 0) {
    u32 tind = alloc_block(vol, preferred_group);
    if(tind == 0)
      return 0;
    inode->i_block[EXT2_TIND_BLOCK] = tind;
    inode->i_blocks += vol->block_size / 512;

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, tind, zero);
      kfree(zero);
    }
  }

  u32 *tind_block = kmalloc(vol->block_size);
  if(!tind_block)
    return 0;

  if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) < 0) {
    kfree(tind_block);
    return 0;
  }

  u32 dind_idx = file_block / (ptrs_per_block * ptrs_per_block);

  if(tind_block[dind_idx] == 0) {
    u32 dind = alloc_block(vol, preferred_group);
    if(dind == 0) {
      kfree(tind_block);
      return 0;
    }
    tind_block[dind_idx] = dind;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, dind, zero);
      kfree(zero);
    }
  }

  u32 dind_block_num = tind_block[dind_idx];
  kfree(tind_block);

  u32 *dind_block = kmalloc(vol->block_size);
  if(!dind_block)
    return 0;

  if(vol_read_block(vol, dind_block_num, dind_block) < 0) {
    kfree(dind_block);
    return 0;
  }

  u32 remaining = file_block % (ptrs_per_block * ptrs_per_block);
  u32 ind_idx   = remaining / ptrs_per_block;

  if(dind_block[ind_idx] == 0) {
    u32 ind = alloc_block(vol, preferred_group);
    if(ind == 0) {
      kfree(dind_block);
      return 0;
    }
    dind_block[ind_idx] = ind;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, dind_block_num, dind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, ind, zero);
      kfree(zero);
    }
  }

  u32 ind_block_num = dind_block[ind_idx];
  kfree(dind_block);

  u32 *ind_block = kmalloc(vol->block_size);
  if(!ind_block)
    return 0;

  if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
    kfree(ind_block);
    return 0;
  }

  u32 ind_offset = remaining % ptrs_per_block;
  if(ind_block[ind_offset] == 0) {
    u32 block = alloc_block(vol, preferred_group);
    if(block == 0) {
      kfree(ind_block);
      return 0;
    }
    ind_block[ind_offset] = block;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, ind_block_num, ind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, block, zero);
      kfree(zero);
    }
  }

  u32 result = ind_block[ind_offset];
  kfree(ind_block);
  return result;
}

/**
 * @brief Free all blocks used by an inode.
 * @param vol Volume.
 * @param inode Inode.
 * @return 0 on success, negative on error.
 */
static i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Free direct blocks */
  for(u32 i = 0; i < EXT2_NDIR_BLOCKS; i++) {
    if(inode->i_block[i]) {
      free_block(vol, inode->i_block[i]);
      inode->i_block[i] = 0;
    }
  }

  /* Free single indirect */
  if(inode->i_block[EXT2_IND_BLOCK]) {
    u32 *ind_block = kmalloc(vol->block_size);
    if(ind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) == 0) {
        for(u32 i = 0; i < ptrs_per_block; i++) {
          if(ind_block[i])
            free_block(vol, ind_block[i]);
        }
      }
      kfree(ind_block);
    }
    free_block(vol, inode->i_block[EXT2_IND_BLOCK]);
    inode->i_block[EXT2_IND_BLOCK] = 0;
  }

  /* Free double indirect */
  if(inode->i_block[EXT2_DIND_BLOCK]) {
    u32 *dind_block = kmalloc(vol->block_size);
    if(dind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) ==
         0) {
        for(u32 i = 0; i < ptrs_per_block; i++) {
          if(dind_block[i]) {
            u32 *ind_block = kmalloc(vol->block_size);
            if(ind_block) {
              if(vol_read_block(vol, dind_block[i], ind_block) == 0) {
                for(u32 j = 0; j < ptrs_per_block; j++) {
                  if(ind_block[j])
                    free_block(vol, ind_block[j]);
                }
              }
              kfree(ind_block);
            }
            free_block(vol, dind_block[i]);
          }
        }
      }
      kfree(dind_block);
    }
    free_block(vol, inode->i_block[EXT2_DIND_BLOCK]);
    inode->i_block[EXT2_DIND_BLOCK] = 0;
  }

  /* Free triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK]) {
    u32 *tind_block = kmalloc(vol->block_size);
    if(tind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) ==
         0) {
        for(u32 t = 0; t < ptrs_per_block; t++) {
          if(tind_block[t]) {
            u32 *dind_block = kmalloc(vol->block_size);
            if(dind_block) {
              if(vol_read_block(vol, tind_block[t], dind_block) == 0) {
                for(u32 d = 0; d < ptrs_per_block; d++) {
                  if(dind_block[d]) {
                    u32 *ind_block = kmalloc(vol->block_size);
                    if(ind_block) {
                      if(vol_read_block(vol, dind_block[d], ind_block) == 0) {
                        for(u32 i = 0; i < ptrs_per_block; i++) {
                          if(ind_block[i])
                            free_block(vol, ind_block[i]);
                        }
                      }
                      kfree(ind_block);
                    }
                    free_block(vol, dind_block[d]);
                  }
                }
              }
              kfree(dind_block);
            }
            free_block(vol, tind_block[t]);
          }
        }
      }
      kfree(tind_block);
    }
    free_block(vol, inode->i_block[EXT2_TIND_BLOCK]);
    inode->i_block[EXT2_TIND_BLOCK] = 0;
  }

  inode->i_blocks = 0;
  return 0;
}

/**
 * @brief Find a directory entry by name.
 * @param vol Volume.
 * @param dir_inode Directory inode.
 * @param name Entry name to find.
 * @param out_ino Output inode number if found.
 * @param out_type Output file type if found.
 * @return 0 on success, -ENOENT if not found.
 */
static i64 dir_find_entry(
    const ext2_volume_t *vol, const ext2_inode_t *dir_inode, const char *name,
    u32 *out_ino, u8 *out_type
)
{
  u32 name_len   = kstrlen(name);
  u32 dir_size   = dir_inode->i_size;
  u32 block_size = vol->block_size;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return -ENOMEM;

  u32 offset = 0;
  while(offset < dir_size) {
    u32 file_block = offset / block_size;
    u32 block_num  = get_block_num(vol, dir_inode, file_block);

    if(block_num == 0) {
      offset += block_size;
      continue;
    }

    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return -EIO;
    }

    u32 block_offset = 0;
    while(block_offset < block_size) {
      const ext2_dirent_t *de =
          (const ext2_dirent_t *)(block_buf + block_offset);

      if(de->rec_len == 0)
        break;

      if(de->inode != 0 && de->name_len == name_len) {
        bool match = true;
        for(u32 i = 0; i < name_len; i++) {
          if(de->name[i] != name[i]) {
            match = false;
            break;
          }
        }
        if(match) {
          *out_ino  = de->inode;
          *out_type = de->file_type;
          kfree(block_buf);
          return 0;
        }
      }

      block_offset += de->rec_len;
    }

    offset += block_size;
  }

  kfree(block_buf);
  return -ENOENT;
}

/**
 * @brief Add a directory entry.
 * @param vol Volume.
 * @param dir_ino Directory inode number.
 * @param dir_inode Directory inode (will be modified).
 * @param name Entry name.
 * @param inode_num Inode number for new entry.
 * @param file_type File type (EXT2_FT_*).
 * @return 0 on success, negative on error.
 */
static i64 dir_add_entry(
    ext2_volume_t *vol, u32 dir_ino, ext2_inode_t *dir_inode, const char *name,
    u32 inode_num, u8 file_type
)
{
  u32 name_len      = kstrlen(name);
  u32 needed_len    = sizeof(ext2_dirent_t) + name_len;
  u32 block_size    = vol->block_size;
  u32 preferred_grp = (dir_ino - 1) / vol->inodes_per_group;

  /* Align to 4 bytes */
  needed_len = (needed_len + 3) & ~3;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return -ENOMEM;

  u32 dir_blocks = (dir_inode->i_size + block_size - 1) / block_size;

  /* Search existing blocks for space */
  for(u32 b = 0; b < dir_blocks; b++) {
    u32 block_num = get_block_num(vol, dir_inode, b);
    if(block_num == 0)
      continue;

    if(vol_read_block(vol, block_num, block_buf) < 0)
      continue;

    u32 offset = 0;
    while(offset < block_size) {
      ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + offset);

      if(de->rec_len == 0)
        break;

      u32 actual_len = sizeof(ext2_dirent_t) + de->name_len;
      actual_len     = (actual_len + 3) & ~3;

      u32 free_space = de->rec_len - actual_len;

      if(free_space >= needed_len) {
        /* Split this entry */
        u32 new_rec_len = de->rec_len - actual_len;
        de->rec_len     = (u16)actual_len;

        ext2_dirent_t *new_de =
            (ext2_dirent_t *)(block_buf + offset + actual_len);
        new_de->inode     = inode_num;
        new_de->rec_len   = (u16)new_rec_len;
        new_de->name_len  = (u8)name_len;
        new_de->file_type = file_type;
        kmemcpy(new_de->name, name, name_len);

        if(vol_write_block(vol, block_num, block_buf) < 0) {
          kfree(block_buf);
          return -EIO;
        }

        kfree(block_buf);
        return 0;
      }

      offset += de->rec_len;
    }
  }

  /* Need to allocate a new block */
  u32 new_block = alloc_file_block(vol, dir_inode, dir_blocks, preferred_grp);
  if(new_block == 0) {
    kfree(block_buf);
    return -ENOSPC;
  }

  kzero(block_buf, block_size);
  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode         = inode_num;
  de->rec_len       = (u16)block_size;
  de->name_len      = (u8)name_len;
  de->file_type     = file_type;
  kmemcpy(de->name, name, name_len);

  if(vol_write_block(vol, new_block, block_buf) < 0) {
    kfree(block_buf);
    return -EIO;
  }

  dir_inode->i_size += block_size;
  if(write_inode(vol, dir_ino, dir_inode) < 0) {
    kfree(block_buf);
    return -EIO;
  }

  kfree(block_buf);
  return 0;
}

/**
 * @brief Remove a directory entry.
 * @param vol Volume.
 * @param dir_inode Directory inode.
 * @param name Entry name to remove.
 * @return 0 on success, negative on error.
 */
static i64 dir_remove_entry(
    const ext2_volume_t *vol, const ext2_inode_t *dir_inode, const char *name
)
{
  u32 name_len   = kstrlen(name);
  u32 dir_size   = dir_inode->i_size;
  u32 block_size = vol->block_size;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return -ENOMEM;

  u32 offset = 0;
  while(offset < dir_size) {
    u32 file_block = offset / block_size;
    u32 block_num  = get_block_num(vol, dir_inode, file_block);

    if(block_num == 0) {
      offset += block_size;
      continue;
    }

    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return -EIO;
    }

    u32            block_offset = 0;
    ext2_dirent_t *prev_de      = NULL;

    while(block_offset < block_size) {
      ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_offset);

      if(de->rec_len == 0)
        break;

      if(de->inode != 0 && de->name_len == name_len) {
        bool match = true;
        for(u32 i = 0; i < name_len; i++) {
          if(de->name[i] != name[i]) {
            match = false;
            break;
          }
        }
        if(match) {
          /* Found it - mark as deleted or merge with previous */
          if(prev_de) {
            prev_de->rec_len += de->rec_len;
          } else {
            de->inode = 0;
          }

          if(vol_write_block(vol, block_num, block_buf) < 0) {
            kfree(block_buf);
            return -EIO;
          }

          kfree(block_buf);
          return 0;
        }
      }

      prev_de = de;
      block_offset += de->rec_len;
    }

    offset += block_size;
  }

  kfree(block_buf);
  return -ENOENT;
}

/**
 * @brief Check if a directory is empty.
 * @param vol Volume.
 * @param dir_inode Directory inode.
 * @return true if empty (only . and ..), false otherwise.
 */
static bool
    dir_is_empty(const ext2_volume_t *vol, const ext2_inode_t *dir_inode)
{
  u32 dir_size   = dir_inode->i_size;
  u32 block_size = vol->block_size;
  u32 count      = 0;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return false;

  u32 offset = 0;
  while(offset < dir_size) {
    u32 file_block = offset / block_size;
    u32 block_num  = get_block_num(vol, dir_inode, file_block);

    if(block_num == 0) {
      offset += block_size;
      continue;
    }

    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return false;
    }

    u32 block_offset = 0;
    while(block_offset < block_size) {
      const ext2_dirent_t *de =
          (const ext2_dirent_t *)(block_buf + block_offset);

      if(de->rec_len == 0)
        break;

      if(de->inode != 0) {
        /* Skip . and .. */
        if(!(de->name_len == 1 && de->name[0] == '.') &&
           !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
          count++;
        }
      }

      block_offset += de->rec_len;
    }

    offset += block_size;
  }

  kfree(block_buf);
  return count == 0;
}

/**
 * @brief Read the target of a symlink inode into a buffer.
 *
 * Handles both fast symlinks (target ≤ 60 bytes, stored inline in i_block[])
 * and slow symlinks (target stored in the first data block).
 *
 * @param vol    Volume.
 * @param inode  Symlink inode.
 * @param buf    Output buffer.
 * @param bufsz  Buffer size.
 * @return 0 on success, negative errno on error.
 */
static i64 read_symlink_target(
    const ext2_volume_t *vol, const ext2_inode_t *inode, char *buf, u32 bufsz
)
{
  u32 len = inode->i_size;
  if(len == 0 || len >= bufsz)
    return -EINVAL;

  if(len <= 60) {
    /* Fast symlink: target stored inline in the i_block[] array. */
    kmemcpy(buf, (const u8 *)inode->i_block, len);
  } else {
    /* Slow symlink: target stored in the first data block. */
    u8 *blk = cache_get_block(vol->block_size);
    if(!blk)
      return -ENOMEM;
    if(vol_read_block(vol, inode->i_block[0], blk) < 0) {
      cache_put_block(blk);
      return -EIO;
    }
    if(len > vol->block_size)
      len = (u32)vol->block_size;
    if(len >= bufsz)
      len = bufsz - 1;
    kmemcpy(buf, blk, len);
    cache_put_block(blk);
  }
  buf[len] = '\0';
  return 0;
}

/**
 * @brief Build a resolved path from a base path and a (possibly relative)
 *        symlink target.
 *
 * If @p target is absolute it is used directly.  If relative, it is joined
 * to the directory part of @p base (i.e. everything up to and including the
 * last '/').
 *
 * @param base    Path that contained the symlink (relative to mount root).
 * @param target  Symlink target as read from the inode.
 * @param out     Output buffer.
 * @param outsz   Output buffer size.
 */
static void build_symlink_path(
    const char *base, const char *target, char *out, u32 outsz
)
{
  if(target[0] == '/') {
    kstrncpy(out, target, outsz);
    return;
  }

  /* Find the last '/' in base to get the parent directory. */
  u32 last_slash = 0;
  for(u32 i = 0; base[i]; i++) {
    if(base[i] == '/')
      last_slash = i + 1; /* include the slash */
  }

  if(last_slash >= outsz) {
    kstrncpy(out, target, outsz);
    return;
  }

  kmemcpy(out, base, last_slash);
  kstrncpy(out + last_slash, target, outsz - last_slash);
}

/**
 * @brief Resolve a path to an inode, following symlinks.
 * @param vol          Volume.
 * @param path         Path to resolve (relative to volume root).
 * @param out_ino      Output inode number.
 * @param out_inode    Output inode structure.
 * @param follow_depth Current symlink-follow depth (prevents loops).
 * @return 0 on success, negative errno on error.
 */
#define SYMLINK_MAX_FOLLOW 8

static i64 resolve_path_depth(
    const ext2_volume_t *vol, const char *path, u32 *out_ino,
    ext2_inode_t *out_inode, int follow_depth
)
{
  if(follow_depth > SYMLINK_MAX_FOLLOW)
    return -ELOOP;

  u32          current_ino = EXT2_ROOT_INODE;
  ext2_inode_t current_inode;

  if(read_inode(vol, current_ino, &current_inode) < 0)
    return -EIO;

  /* Skip leading slash */
  if(path[0] == '/')
    path++;

  /* Empty path = root */
  if(path[0] == '\0') {
    *out_ino   = current_ino;
    *out_inode = current_inode;
    return 0;
  }

  /* Keep a working copy of the full path for symlink target construction. */
  char work[VFS_PATH_MAX];
  kstrncpy(work, path, VFS_PATH_MAX);
  const char *p = work;

  char        component[EXT2_NAME_MAX + 1];

  while(*p) {
    /* Skip slashes */
    while(*p == '/')
      p++;
    if(*p == '\0')
      break;

    /* Extract component */
    u32 i = 0;
    while(*p && *p != '/' && i < EXT2_NAME_MAX) {
      component[i++] = *p++;
    }
    component[i] = '\0';

    /* Must be a directory to traverse */
    if((current_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
      return -ENOTDIR;

    /* Find entry */
    u32 entry_ino;
    u8  entry_type;
    if(dir_find_entry(vol, &current_inode, component, &entry_ino, &entry_type) <
       0)
      return -ENOENT;

    current_ino = entry_ino;
    if(read_inode(vol, current_ino, &current_inode) < 0)
      return -EIO;

    /* Follow symlinks (both intermediate and final). */
    if((current_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK) {
      char target[VFS_PATH_MAX];
      if(read_symlink_target(vol, &current_inode, target, sizeof(target)) < 0)
        return -EIO;

      /* Position of this component in `work`: just before component start. */
      u32 comp_offset = (u32)(p - work) - kstrlen(component);
      /* Remaining path after symlink component (may be empty or "/..."). */
      const char *rest = p;

      /* Build: parent(work[0..comp_offset-1]) + target + rest */
      char followed[VFS_PATH_MAX];
      char base_for_link[VFS_PATH_MAX];
      kstrncpy(
          base_for_link, work,
          comp_offset < VFS_PATH_MAX ? comp_offset : VFS_PATH_MAX
      );
      if(comp_offset < VFS_PATH_MAX)
        base_for_link[comp_offset] = '\0';

      /* Append a dummy filename so build_symlink_path strips correctly. */
      u32 bl = kstrlen(base_for_link);
      if(bl + 8 < VFS_PATH_MAX) {
        base_for_link[bl]     = '/';
        base_for_link[bl + 1] = 'X'; /* dummy */
        base_for_link[bl + 2] = '\0';
      }
      build_symlink_path(base_for_link, target, followed, VFS_PATH_MAX);

      /* Append remaining path components. */
      if(*rest) {
        u32 flen = kstrlen(followed);
        if(flen + 1 + kstrlen(rest) < VFS_PATH_MAX) {
          followed[flen] = '/';
          kstrncpy(followed + flen + 1, rest, VFS_PATH_MAX - flen - 1);
        }
      }

      return resolve_path_depth(
          vol, followed, out_ino, out_inode, follow_depth + 1
      );
    }
  }

  *out_ino   = current_ino;
  *out_inode = current_inode;
  return 0;
}

/**
 * @brief Resolve a path to an inode.
 * @param vol Volume.
 * @param path Path to resolve.
 * @param out_ino Output inode number.
 * @param out_inode Output inode structure.
 * @return 0 on success, negative errno on error.
 */
static i64 resolve_path(
    const ext2_volume_t *vol, const char *path, u32 *out_ino,
    ext2_inode_t *out_inode
)
{
  return resolve_path_depth(vol, path, out_ino, out_inode, 0);
}

/**
 * @brief Get parent directory path and filename from a path.
 * @param path Full path.
 * @param parent Output buffer for parent path.
 * @param name Output buffer for filename.
 */
static void path_split(const char *path, char *parent, char *name)
{
  u32 len        = kstrlen(path);
  i32 last_slash = -1;

  for(u32 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i32)i;
  }

  if(last_slash <= 0) {
    parent[0] = '/';
    parent[1] = '\0';
    if(path[0] == '/')
      kstrncpy(name, path + 1, EXT2_NAME_MAX);
    else
      kstrncpy(name, path, EXT2_NAME_MAX);
  } else {
    kmemcpy(parent, path, (u64)last_slash);
    parent[last_slash] = '\0';
    kstrncpy(name, path + last_slash + 1, EXT2_NAME_MAX);
  }
}

/**
 * @brief Initialize the ext2 filesystem driver.
 *
 * Clears volume/file pools and registers ext2 with the VFS.
 */
void ext2_init(void)
{
  kzero(g_volumes, sizeof(g_volumes));
  kzero(g_files, sizeof(g_files));
  vfs_register_fs(&g_ext2_fstype);
}

/**
 * @brief Mount an ext2 partition.
 *
 * Reads the superblock and group descriptors from disk.
 *
 * @param drive ATA drive number.
 * @param partition_lba LBA offset of the partition.
 * @return Volume handle, or NULL on failure.
 */
ext2_volume_t *ext2_mount(u8 drive, u32 partition_lba)
{
  /* Find free volume slot */
  ext2_volume_t *vol = NULL;
  for(int i = 0; i < EXT2_MAX_VOLUMES; i++) {
    if(!g_volumes[i].mounted) {
      vol = &g_volumes[i];
      break;
    }
  }

  if(!vol) {
    console_print("[EXT2] No free volume slots\n");
    return NULL;
  }

  /* Read superblock (at byte 1024, sectors 2-3) */
  u8 sb_buf[EXT2_MIN_BLOCK_SIZE];
  if(ata_read(drive, partition_lba + 2, 2, sb_buf) < 0) {
    console_print("[EXT2] Failed to read superblock\n");
    return NULL;
  }

  ext2_superblock_t *sb = (ext2_superblock_t *)sb_buf;

  /* Verify magic */
  if(sb->s_magic != EXT2_MAGIC) {
    console_print("[EXT2] Invalid magic number\n");
    return NULL;
  }

  /* Fill volume info */
  vol->drive            = drive;
  vol->partition_lba    = partition_lba;
  vol->block_size       = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;
  vol->blocks_per_group = sb->s_blocks_per_group;
  vol->inodes_per_group = sb->s_inodes_per_group;
  vol->inode_size       = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
  vol->inodes_count     = sb->s_inodes_count;
  vol->blocks_count     = sb->s_blocks_count;
  vol->first_data_block = sb->s_first_data_block;
  vol->groups_count     = (sb->s_blocks_count + sb->s_blocks_per_group - 1) /
                      sb->s_blocks_per_group;

  kmemcpy(&vol->sb, sb, sizeof(ext2_superblock_t));

  /* Read group descriptors */
  u32 gdt_block  = vol->first_data_block + 1;
  u32 gdt_size   = vol->groups_count * sizeof(ext2_group_desc_t);
  u32 gdt_blocks = (gdt_size + vol->block_size - 1) / vol->block_size;

  vol->groups = kmalloc((u64)vol->groups_count * sizeof(ext2_group_desc_t));
  if(!vol->groups) {
    console_print("[EXT2] Failed to allocate group descriptors\n");
    return NULL;
  }

  u8 *gdt_buf = kmalloc((u64)gdt_blocks * vol->block_size);
  if(!gdt_buf) {
    kfree(vol->groups);
    console_print("[EXT2] Failed to allocate GDT buffer\n");
    return NULL;
  }

  for(u32 b = 0; b < gdt_blocks; b++) {
    if(vol_read_block(vol, gdt_block + b, gdt_buf + (u64)b * vol->block_size) <
       0) {
      kfree(gdt_buf);
      kfree(vol->groups);
      console_print("[EXT2] Failed to read group descriptors\n");
      return NULL;
    }
  }

  kmemcpy(vol->groups, gdt_buf, vol->groups_count * sizeof(ext2_group_desc_t));
  kfree(gdt_buf);

  vol->mounted = true;

  console_printf(
      "[EXT2] Mounted: %u blocks, %u inodes, %u block size\n",
      vol->blocks_count, vol->inodes_count, vol->block_size
  );

  return vol;
}

/**
 * @brief Unmount an ext2 volume.
 *
 * Flushes metadata and frees group descriptor memory.
 *
 * @param vol Volume to unmount.
 */
void ext2_unmount(ext2_volume_t *vol)
{
  if(!vol || !vol->mounted)
    return;

  /* Write back superblock and group descriptors */
  write_superblock(vol);
  write_group_descriptors(vol);

  if(vol->groups) {
    kfree(vol->groups);
    vol->groups = NULL;
  }

  vol->mounted = false;
}

/**
 * @brief Open a file or directory on an ext2 volume.
 *
 * @param vol  Volume handle.
 * @param path Path to open.
 * @return File handle, or NULL on failure.
 */
ext2_file_t *ext2_open(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return NULL;

  /* Find free file slot */
  ext2_file_t *file = NULL;
  for(int i = 0; i < EXT2_MAX_FILES; i++) {
    if(!g_files[i].in_use) {
      file = &g_files[i];
      break;
    }
  }

  if(!file)
    return NULL;

  /* Resolve path */
  u32          ino;
  ext2_inode_t inode;
  if(resolve_path(vol, path, &ino, &inode) < 0)
    return NULL;

  /* Fill file handle */
  file->vol          = vol;
  file->inode_num    = ino;
  file->inode        = inode;
  file->position     = 0;
  file->block_offset = 0;
  file->is_dir       = (inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
  file->in_use       = true;
  file->dirty        = false;

  return file;
}

/**
 * @brief Close an ext2 file handle.
 *
 * Writes back dirty inode data before releasing the handle.
 *
 * @param file File handle to close.
 */
void ext2_close(ext2_file_t *file)
{
  if(!file || !file->in_use)
    return;

  if(file->dirty) {
    write_inode(file->vol, file->inode_num, &file->inode);
    flush_metadata(file->vol);
  }

  file->in_use = false;
}

/**
 * @brief Read data from an ext2 file.
 *
 * @param file  Open file handle.
 * @param buf   Destination buffer.
 * @param count Maximum bytes to read.
 * @return Bytes read, or negative errno on error.
 */
i64 ext2_read(ext2_file_t *file, void *buf, u64 count, u64 offset)
{
  if(!file || !file->in_use || file->is_dir)
    return -EINVAL;

  const ext2_volume_t *vol        = file->vol;
  u8                  *dst        = (u8 *)buf;
  u64                  bytes_read = 0;
  u32                  block_size = vol->block_size;

  /* Limit to file size */
  if(offset >= file->inode.i_size)
    return 0;
  if(offset + count > file->inode.i_size)
    count = file->inode.i_size - offset;

  u8 *block_buf = cache_get_block(block_size);
  if(!block_buf)
    return -ENOMEM;

  /* Multi-block read buffer (allocated once, optional fallback). */
  u8 *run_buf = kmalloc((u64)EXT2_READ_RUN_MAX * block_size);

  while(bytes_read < count) {
    u64 current_pos  = offset + bytes_read;
    u32 file_block   = current_pos / block_size;
    u32 block_offset = current_pos % block_size;
    u32 block_num    = get_block_num(vol, &file->inode, file_block);

    if(block_num == 0) {
      /* Sparse file - return zeros */
      u64 to_read = block_size - block_offset;
      if(to_read > count - bytes_read)
        to_read = count - bytes_read;
      kzero(dst + bytes_read, to_read);
      bytes_read += to_read;
      continue;
    }

    u64 remaining = count - bytes_read;

    if(run_buf) {
      /* Multi-block fast path. */
      u32 max_run =
          (u32)((remaining + block_offset + block_size - 1) / block_size);
      if(max_run > EXT2_READ_RUN_MAX)
        max_run = EXT2_READ_RUN_MAX;

      /* Detect how many consecutive disk blocks follow block_num. */
      u32 run = 1;
      while(run < max_run) {
        u32 nxt = get_block_num(vol, &file->inode, file_block + run);
        if(nxt != block_num + run)
          break;
        run++;
      }

      if(run == 1) {
        /* Only one block (or fragmented): use cached single-block read. */
        if(vol_read_block(vol, block_num, block_buf) < 0) {
          kfree(run_buf);
          cache_put_block(block_buf);
          return bytes_read > 0 ? (i64)bytes_read : -EIO;
        }
        u64 to_read = block_size - block_offset;
        if(to_read > remaining)
          to_read = remaining;
        kmemcpy(dst + bytes_read, block_buf + block_offset, to_read);
        bytes_read += to_read;
      } else {
        /* Multi-block DMA: read `run` consecutive disk blocks at once. */
        u32 sectors  = run * (block_size / EXT2_SECTOR_SIZE);
        u32 base_sec = block_num * (block_size / EXT2_SECTOR_SIZE);
        if(vol_read_sectors(vol, base_sec, sectors, run_buf) < 0) {
          kfree(run_buf);
          cache_put_block(block_buf);
          return bytes_read > 0 ? (i64)bytes_read : -EIO;
        }
        /* Populate cache with freshly read blocks. */
        for(u32 i = 0; i < run; i++)
          dsk_cache_insert(
              vol, block_num + i, run_buf + (u64)i * block_size, block_size
          );

        u64 avail   = (u64)run * block_size - block_offset;
        u64 to_read = remaining < avail ? remaining : avail;
        kmemcpy(dst + bytes_read, run_buf + block_offset, to_read);
        bytes_read += to_read;
      }
    } else {
      /* run_buf unavailable: fall back to single-block reads. */
      if(vol_read_block(vol, block_num, block_buf) < 0) {
        cache_put_block(block_buf);
        return bytes_read > 0 ? (i64)bytes_read : -EIO;
      }
      u64 to_read = block_size - block_offset;
      if(to_read > remaining)
        to_read = remaining;
      kmemcpy(dst + bytes_read, block_buf + block_offset, to_read);
      bytes_read += to_read;
    }
  }

  if(run_buf)
    kfree(run_buf);
  cache_put_block(block_buf);
  return (i64)bytes_read;
}

/**
 * @brief Write data to an ext2 file.
 *
 * Allocates new blocks as needed and extends the file.
 *
 * @param file  Open file handle.
 * @param buf   Source buffer.
 * @param count Number of bytes to write.
 * @return Bytes written, or negative errno on error.
 */
i64 ext2_write(ext2_file_t *file, const void *buf, u64 count, u64 offset)
{
  if(!file || !file->in_use || file->is_dir)
    return -EINVAL;

  if(count == 0)
    return 0;

  ext2_volume_t *vol           = file->vol;
  const u8      *src           = (const u8 *)buf;
  u64            bytes_written = 0;
  u32            block_size    = vol->block_size;
  u32            preferred_grp = (file->inode_num - 1) / vol->inodes_per_group;

  u8            *block_buf = cache_get_block(block_size);
  if(!block_buf)
    return -ENOMEM;

  while(bytes_written < count) {
    u64 current_pos  = offset + bytes_written;
    u32 file_block   = current_pos / block_size;
    u32 block_offset = current_pos % block_size;

    /* Allocate block if needed */
    u32 block_num = get_block_num(vol, &file->inode, file_block);
    if(block_num == 0) {
      block_num =
          alloc_file_block(vol, &file->inode, file_block, preferred_grp);
      if(block_num == 0) {
        cache_put_block(block_buf);
        return bytes_written > 0 ? (i64)bytes_written : -ENOSPC;
      }
      file->dirty = true;
    }

    /* Read existing block for partial write */
    if(block_offset != 0 || (count - bytes_written) < block_size) {
      if(vol_read_block(vol, block_num, block_buf) < 0) {
        cache_put_block(block_buf);
        return bytes_written > 0 ? (i64)bytes_written : -EIO;
      }
    }

    u64 to_write = block_size - block_offset;
    if(to_write > count - bytes_written)
      to_write = count - bytes_written;

    kmemcpy(block_buf + block_offset, src + bytes_written, to_write);

    if(vol_write_block(vol, block_num, block_buf) < 0) {
      cache_put_block(block_buf);
      return bytes_written > 0 ? (i64)bytes_written : -EIO;
    }

    bytes_written += to_write;

    if(current_pos + to_write > file->inode.i_size) {
      file->inode.i_size = current_pos + to_write;
      file->dirty        = true;
    }
  }

  cache_put_block(block_buf);

  if(file->dirty) {
    write_inode(vol, file->inode_num, &file->inode);
  }

  return (i64)bytes_written;
}

/**
 * @brief Read the next directory entry.
 *
 * @param dir   Open directory handle.
 * @param entry Output entry structure.
 * @return 1 if an entry was read, 0 at end, or negative errno on error.
 */
i64 ext2_readdir(ext2_file_t *dir, u64 index, ext2_entry_t *entry)
{
  if(!dir || !dir->in_use || !dir->is_dir)
    return -EINVAL;

  const ext2_volume_t *vol        = dir->vol;
  u32                  block_size = vol->block_size;

  u8                  *block_buf = cache_get_block(block_size);
  if(!block_buf)
    return -ENOMEM;

  u32 current_entry = 0;
  u32 pos           = 0;

  while(pos < dir->inode.i_size) {
    u32 file_block   = pos / block_size;
    u32 block_offset = pos % block_size;
    u32 block_num    = get_block_num(vol, &dir->inode, file_block);

    if(block_num == 0) {
      pos = (file_block + 1) * block_size;
      continue;
    }

    if(vol_read_block(vol, block_num, block_buf) < 0) {
      cache_put_block(block_buf);
      return -EIO;
    }

    const ext2_dirent_t *de = (const ext2_dirent_t *)(block_buf + block_offset);

    if(de->rec_len == 0) {
      pos = (file_block + 1) * block_size;
      continue;
    }

    if(de->inode != 0) {
      if(current_entry == index) {
        /* Copy entry info */
        u32 name_len = de->name_len;
        if(name_len > EXT2_NAME_MAX)
          name_len = EXT2_NAME_MAX;
        kmemcpy(entry->name, de->name, name_len);
        entry->name[name_len] = '\0';
        entry->inode          = de->inode;
        entry->file_type      = de->file_type;

        /* Get file size */
        ext2_inode_t file_inode;
        if(read_inode(vol, de->inode, &file_inode) == 0) {
          entry->size = file_inode.i_size;
        } else {
          entry->size = 0;
        }

        cache_put_block(block_buf);
        return 1;
      }
      current_entry++;
    }

    pos += de->rec_len;
  }

  cache_put_block(block_buf);
  return 0;
}

/**
 * @brief Get file status information.
 *
 * @param vol   Volume handle.
 * @param path  Path to the file or directory.
 * @param entry Output entry with inode, size, and type.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_stat(const ext2_volume_t *vol, const char *path, ext2_entry_t *entry)
{
  if(!vol || !vol->mounted || !path || !entry)
    return -EINVAL;

  u32          ino;
  ext2_inode_t inode;
  if(resolve_path(vol, path, &ino, &inode) < 0)
    return -ENOENT;

  /* Extract filename from path */
  u32 len        = kstrlen(path);
  i32 last_slash = -1;
  for(u32 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i32)i;
  }
  if(last_slash == -1 || (u32)(last_slash + 1) >= len) {
    kstrncpy(entry->name, path, EXT2_NAME_MAX);
  } else {
    kstrncpy(entry->name, path + last_slash + 1, EXT2_NAME_MAX);
  }

  entry->inode = ino;
  entry->size  = inode.i_size;

  u16 mode = inode.i_mode & EXT2_S_IFMT;
  switch(mode) {
  case EXT2_S_IFREG:
    entry->file_type = EXT2_FT_REG_FILE;
    break;
  case EXT2_S_IFDIR:
    entry->file_type = EXT2_FT_DIR;
    break;
  case EXT2_S_IFLNK:
    entry->file_type = EXT2_FT_SYMLINK;
    break;
  case EXT2_S_IFCHR:
    entry->file_type = EXT2_FT_CHRDEV;
    break;
  case EXT2_S_IFBLK:
    entry->file_type = EXT2_FT_BLKDEV;
    break;
  case EXT2_S_IFIFO:
    entry->file_type = EXT2_FT_FIFO;
    break;
  case EXT2_S_IFSOCK:
    entry->file_type = EXT2_FT_SOCK;
    break;
  default:
    entry->file_type = EXT2_FT_UNKNOWN;
    break;
  }

  return 0;
}

i64 ext2_readlink(
    const ext2_volume_t *vol, const char *path, char *buf, u64 cap
)
{
  if(!vol || !vol->mounted || !path || !buf || cap == 0)
    return -EINVAL;

  char parent_path[EXT2_NAME_MAX + 1];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);

  if(filename[0] == '\0')
    return -EINVAL;

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;

  if((parent_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;

  u32 entry_ino;
  u8  entry_type;
  if(dir_find_entry(vol, &parent_inode, filename, &entry_ino, &entry_type) < 0)
    return -ENOENT;

  ext2_inode_t inode;
  if(read_inode(vol, entry_ino, &inode) < 0)
    return -EIO;

  if((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK)
    return -EINVAL;

  char target[VFS_PATH_MAX];
  i64  tlen = read_symlink_target(vol, &inode, target, sizeof(target));
  if(tlen < 0)
    return -EIO;

  u64 n = (u64)tlen;
  if(n >= cap)
    return -ENAMETOOLONG;

  kmemcpy(buf, target, n);
  return (i64)n;
}

/**
 * @brief Seek to a position in an ext2 file.
 *
 * @param file   Open file handle.
 * @param offset Seek offset.
 * @param whence Seek mode (0=SET, 1=CUR, 2=END).
 * @return New position, or negative errno on error.
 */
i64 ext2_seek(ext2_file_t *file, i64 offset, i32 whence)
{
  if(!file || !file->in_use)
    return -EINVAL;

  i64 new_pos;
  switch(whence) {
  case 0: /* SEEK_SET */
    new_pos = offset;
    break;
  case 1: /* SEEK_CUR */
    new_pos = (i64)file->position + offset;
    break;
  case 2: /* SEEK_END */
    new_pos = (i64)file->inode.i_size + offset;
    break;
  default:
    return -EINVAL;
  }

  if(new_pos < 0)
    return -EINVAL;

  file->position = (u32)new_pos;
  return (i64)file->position;
}

/**
 * @brief Create a new file on an ext2 volume.
 *
 * If the file already exists, opens it instead.
 *
 * @param vol  Volume handle.
 * @param path Path for the new file.
 * @return File handle, or NULL on failure.
 */
ext2_file_t *ext2_create(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return NULL;

  /* Check if file already exists */
  u32          existing_ino;
  ext2_inode_t existing_inode;
  if(resolve_path(vol, path, &existing_ino, &existing_inode) == 0) {
    /* File exists, just open it */
    return ext2_open(vol, path);
  }

  /* Find free file slot */
  ext2_file_t *file = NULL;
  for(int i = 0; i < EXT2_MAX_FILES; i++) {
    if(!g_files[i].in_use) {
      file = &g_files[i];
      break;
    }
  }

  if(!file)
    return NULL;

  /* Split path into parent and name */
  char parent_path[EXT2_NAME_MAX + 1];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);

  if(filename[0] == '\0')
    return NULL;

  /* Resolve parent directory */
  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return NULL;

  if((parent_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return NULL;

  /* Allocate new inode */
  u32 preferred_grp = (parent_ino - 1) / vol->inodes_per_group;
  u32 new_ino       = alloc_inode(vol, preferred_grp, false);
  if(new_ino == 0)
    return NULL;

  /* Initialize new inode */
  ext2_inode_t new_inode;
  kzero(&new_inode, sizeof(ext2_inode_t));
  new_inode.i_mode        = EXT2_S_IFREG | 0644;
  new_inode.i_uid         = 0;
  new_inode.i_gid         = 0;
  new_inode.i_size        = 0;
  new_inode.i_links_count = 1;
  new_inode.i_blocks      = 0;

  if(write_inode(vol, new_ino, &new_inode) < 0) {
    free_inode(vol, new_ino, false);
    return NULL;
  }

  /* Add entry to parent directory */
  if(dir_add_entry(
         vol, parent_ino, &parent_inode, filename, new_ino, EXT2_FT_REG_FILE
     ) < 0) {
    free_inode(vol, new_ino, false);
    return NULL;
  }

  flush_metadata(vol);

  /* Fill file handle */
  file->vol          = vol;
  file->inode_num    = new_ino;
  file->inode        = new_inode;
  file->position     = 0;
  file->block_offset = 0;
  file->is_dir       = false;
  file->in_use       = true;
  file->dirty        = false;

  return file;
}

/**
 * @brief Create a directory on an ext2 volume.
 *
 * @param vol  Volume handle.
 * @param path Path for the new directory.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_mkdir(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  /* Check if already exists */
  u32          existing_ino;
  ext2_inode_t existing_inode;
  if(resolve_path(vol, path, &existing_ino, &existing_inode) == 0)
    return -EEXIST;

  /* Split path */
  char parent_path[EXT2_NAME_MAX + 1];
  char dirname[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, dirname);

  if(dirname[0] == '\0')
    return -EINVAL;

  /* Resolve parent */
  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;

  if((parent_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;

  /* Allocate new inode */
  u32 preferred_grp = (parent_ino - 1) / vol->inodes_per_group;
  u32 new_ino       = alloc_inode(vol, preferred_grp, true);
  if(new_ino == 0)
    return -ENOSPC;

  /* Allocate first block for directory */
  ext2_inode_t new_inode;
  kzero(&new_inode, sizeof(ext2_inode_t));
  new_inode.i_mode        = EXT2_S_IFDIR | 0755;
  new_inode.i_uid         = 0;
  new_inode.i_gid         = 0;
  new_inode.i_size        = vol->block_size;
  new_inode.i_links_count = 2; /* . and parent's link */

  u32 first_block = alloc_block(vol, preferred_grp);
  if(first_block == 0) {
    free_inode(vol, new_ino, true);
    return -ENOSPC;
  }

  new_inode.i_block[0] = first_block;
  new_inode.i_blocks   = vol->block_size / 512;

  /* Create . and .. entries */
  u8 *block_buf = kmalloc(vol->block_size);
  if(!block_buf) {
    free_block(vol, first_block);
    free_inode(vol, new_ino, true);
    return -ENOMEM;
  }

  kzero(block_buf, vol->block_size);

  /* . entry */
  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode         = new_ino;
  de->rec_len       = 12;
  de->name_len      = 1;
  de->file_type     = EXT2_FT_DIR;
  de->name[0]       = '.';

  /* .. entry */
  de            = (ext2_dirent_t *)(block_buf + 12);
  de->inode     = parent_ino;
  de->rec_len   = (u16)(vol->block_size - 12);
  de->name_len  = 2;
  de->file_type = EXT2_FT_DIR;
  de->name[0]   = '.';
  de->name[1]   = '.';

  if(vol_write_block(vol, first_block, block_buf) < 0) {
    kfree(block_buf);
    free_block(vol, first_block);
    free_inode(vol, new_ino, true);
    return -EIO;
  }

  kfree(block_buf);

  /* Write new inode */
  if(write_inode(vol, new_ino, &new_inode) < 0) {
    free_block(vol, first_block);
    free_inode(vol, new_ino, true);
    return -EIO;
  }

  /* Add entry to parent */
  if(dir_add_entry(
         vol, parent_ino, &parent_inode, dirname, new_ino, EXT2_FT_DIR
     ) < 0) {
    free_block(vol, first_block);
    free_inode(vol, new_ino, true);
    return -EIO;
  }

  /* Increment parent link count (for ..) */
  parent_inode.i_links_count++;
  write_inode(vol, parent_ino, &parent_inode);

  flush_metadata(vol);
  return 0;
}

/**
 * @brief Truncate an ext2 file to @p length bytes.
 *
 * If shrinking past zero, free all data blocks; if extending, simply
 * record the new size — ext2_read returns zeros for unallocated holes,
 * matching POSIX sparse-file semantics. (Future: free blocks beyond the
 * new tail when shrinking to a non-zero size — for now the user payload
 * we care about is "extend to N then write N bytes" which is the lld
 * codepath.)
 *
 * @param file   Open file handle.
 * @param length Target length in bytes.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_truncate(ext2_file_t *file, u64 length)
{
  if(!file || !file->in_use || file->is_dir)
    return -EINVAL;

  ext2_volume_t *vol = file->vol;

  if(length == 0) {
    /* Free all data blocks. */
    free_inode_blocks(vol, &file->inode);
  }

  file->inode.i_size = (u32)length;
  if(file->position > length)
    file->position = length;
  file->dirty = false; /* Inode is written below. */

  if(write_inode(vol, file->inode_num, &file->inode) < 0)
    return -EIO;

  return flush_metadata(vol);
}

/**
 * @brief Flush a file's dirty inode and metadata to disk.
 *
 * @param file Open file handle.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_flush(ext2_file_t *file)
{
  if(!file || !file->in_use)
    return -EINVAL;

  if(!file->dirty)
    return 0;

  if(write_inode(file->vol, file->inode_num, &file->inode) < 0)
    return -EIO;

  if(flush_metadata(file->vol) < 0)
    return -EIO;

  file->dirty = false;
  return 0;
}

/**
 * @brief Remove a file from an ext2 volume.
 *
 * Decrements the link count; frees blocks/inode when it reaches zero.
 *
 * @param vol  Volume handle.
 * @param path Path to the file.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_unlink(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  /* Resolve the file */
  u32          file_ino;
  ext2_inode_t file_inode;
  if(resolve_path(vol, path, &file_ino, &file_inode) < 0)
    return -ENOENT;

  /* Can't unlink directories */
  if((file_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return -EISDIR;

  /* Get parent directory */
  char parent_path[EXT2_NAME_MAX + 1];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;

  /* Remove directory entry */
  if(dir_remove_entry(vol, &parent_inode, filename) < 0)
    return -EIO;

  /* Decrement link count */
  file_inode.i_links_count--;

  if(file_inode.i_links_count == 0) {
    /* Free all blocks and inode */
    free_inode_blocks(vol, &file_inode);
    free_inode(vol, file_ino, false);
  } else {
    write_inode(vol, file_ino, &file_inode);
  }

  flush_metadata(vol);
  return 0;
}

/**
 * @brief Remove an empty directory from an ext2 volume.
 *
 * @param vol  Volume handle.
 * @param path Path to the directory.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_rmdir(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  /* Can't remove root */
  if(kstrcmp(path, "/") == 0)
    return -EINVAL;

  /* Resolve the directory */
  u32          dir_ino;
  ext2_inode_t dir_inode;
  if(resolve_path(vol, path, &dir_ino, &dir_inode) < 0)
    return -ENOENT;

  /* Must be a directory */
  if((dir_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;

  /* Must be empty */
  if(!dir_is_empty(vol, &dir_inode))
    return -ENOTEMPTY;

  /* Get parent directory */
  char parent_path[EXT2_NAME_MAX + 1];
  char dirname[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, dirname);

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;

  /* Remove directory entry from parent */
  if(dir_remove_entry(vol, &parent_inode, dirname) < 0)
    return -EIO;

  /* Decrement parent link count (for ..) */
  parent_inode.i_links_count--;
  write_inode(vol, parent_ino, &parent_inode);

  /* Free directory blocks and inode */
  free_inode_blocks(vol, &dir_inode);
  free_inode(vol, dir_ino, true);

  flush_metadata(vol);
  return 0;
}

/* VFS operation wrappers */

/**
 * @name VFS filesystem operations
 *
 * Wrapper functions that adapt ext2 API to the VFS fs_ops interface.
 * @{
 */

static fs_handle_t ext2_vfs_open(void *fs_data, const char *path, u32 flags)
{
  ext2_volume_t *vol = (ext2_volume_t *)fs_data;
  if(flags & O_CREAT) {
    return (fs_handle_t)ext2_create(vol, path);
  }
  return (fs_handle_t)ext2_open(vol, path);
}

static void ext2_vfs_close(fs_handle_t fh)
{
  ext2_close((ext2_file_t *)fh);
}

static i64 ext2_vfs_read(fs_handle_t fh, void *buf, u64 count, u64 offset)
{
  return ext2_read((ext2_file_t *)fh, buf, count, offset);
}

static i64
    ext2_vfs_write(fs_handle_t fh, const void *buf, u64 count, u64 offset)
{
  return ext2_write((ext2_file_t *)fh, buf, count, offset);
}

static i64 ext2_vfs_mkdir(void *fs_data, const char *path)
{
  return ext2_mkdir((ext2_volume_t *)fs_data, path);
}

static i64 ext2_vfs_unlink(void *fs_data, const char *path)
{
  return ext2_unlink((ext2_volume_t *)fs_data, path);
}

static i64 ext2_vfs_rmdir(void *fs_data, const char *path)
{
  return ext2_rmdir((ext2_volume_t *)fs_data, path);
}

static i64 ext2_vfs_fstat(fs_handle_t fh, vfs_stat_t *st)
{
  ext2_file_t *f = (ext2_file_t *)fh;
  if(!f || !f->in_use || !st)
    return -EINVAL;

  st->size     = f->inode.i_size;
  st->type     = f->is_dir ? VFS_DIRECTORY : VFS_FILE;
  st->created  = 0;
  st->modified = 0;
  st->ino      = f->inode_num;
  st->dev      = 0;
  return 0;
}

static i64 ext2_vfs_stat(void *fs_data, const char *path, vfs_stat_t *st)
{
  ext2_entry_t entry;
  i64          ret = ext2_stat((ext2_volume_t *)fs_data, path, &entry);
  if(ret == 0) {
    st->size = entry.size;
    st->type = (entry.file_type == EXT2_FT_DIR) ? VFS_DIRECTORY : VFS_FILE;
    st->ino  = entry.inode;
    st->dev  = 0;
  }
  return ret;
}

static i64
    ext2_vfs_readdir(fs_handle_t fh, u64 index, char *name, vfs_stat_t *st)
{
  ext2_file_t *dir = (ext2_file_t *)fh;
  ext2_entry_t entry;
  i64          ret = ext2_readdir(dir, index, &entry);
  if(ret > 0) {
    kstrncpy(name, entry.name, VFS_NAME_MAX);
    if(st) {
      st->type = (entry.file_type == EXT2_FT_DIR) ? VFS_DIRECTORY : VFS_FILE;
      st->size = entry.size;
      st->ino  = entry.inode;
    }
  }
  return ret;
}

static i64 ext2_vfs_truncate(fs_handle_t fh, u64 length)
{
  return ext2_truncate((ext2_file_t *)fh, length);
}

static i64
    ext2_vfs_readlink(void *fs_data, const char *path, char *buf, u64 cap)
{
  return ext2_readlink((ext2_volume_t *)fs_data, path, buf, cap);
}

/** @brief ext2 VFS operations table. */
static const fs_ops_t g_ext2_vfs_ops = {
    .open     = ext2_vfs_open,
    .close    = ext2_vfs_close,
    .read     = ext2_vfs_read,
    .write    = ext2_vfs_write,
    .mkdir    = ext2_vfs_mkdir,
    .unlink   = ext2_vfs_unlink,
    .rmdir    = ext2_vfs_rmdir,
    .stat     = ext2_vfs_stat,
    .fstat    = ext2_vfs_fstat,
    .readdir  = ext2_vfs_readdir,
    .truncate = ext2_vfs_truncate,
    .readlink = ext2_vfs_readlink,
};

/** @brief ext2 mount wrapper for fs_type_t. */
static void *ext2_vfs_mount(const char *source, u32 flags)
{
  (void)source;
  (void)flags;
  /* Hardcoded to ATA 0 sector 0 (no partition table) */
  return ext2_mount(0, 0);
}

/** @brief ext2 unmount wrapper for fs_type_t. */
static void ext2_vfs_unmount(void *fs_data)
{
  ext2_unmount((ext2_volume_t *)fs_data);
}

/** @brief ext2 filesystem type descriptor. */
static const fs_type_t g_ext2_fstype = {
    .name    = "ext2",
    .ops     = &g_ext2_vfs_ops,
    .mount   = ext2_vfs_mount,
    .unmount = ext2_vfs_unmount,
};
/** @} */

/**
 * @brief Get ext2 VFS operations table.
 * @return Pointer to ext2 fs_ops_t structure.
 */
// cppcheck-suppress unusedFunction
const fs_ops_t *ext2_get_ops(void)
{
  return &g_ext2_vfs_ops;
}
