/**
 * @file src/fs/ext2/super.c
 * @brief Block-level I/O, superblock + GDT writeback, volume bring-up.
 *
 * Owns the per-volume state (@ref g_volumes) and the default device pointer
 * (@ref g_default_dev), plus the low-level block read/write primitives every
 * other ext2 module routes through. Mount and init also live here because
 * they are the only callers that touch the raw superblock layout.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/** @brief Mounted-volume pool. Sized at @ref EXT2_MAX_VOLUMES to cap the
 * static superblock + GDT footprint. */
ext2_volume_t g_volumes[EXT2_MAX_VOLUMES];

/** @brief First-registered block device; used by @c mount("ext2") when no
 * source path is supplied. */
const blockdev_t *g_default_dev;

/**
 * @brief Read one ext2 block worth of bytes into @p buf.
 *
 * Translates the ext2-level block number to the underlying device's sector
 * range. Every metadata + data read in the driver goes through this so the
 * block-to-sector ratio lives in exactly one place.
 *
 * @param vol    Source volume.
 * @param block  Ext2 block number.
 * @param buf    Destination buffer (≥ @c vol->block_size bytes).
 * @return Bytes transferred on success, negative errno on I/O failure.
 */
i64 vol_read_block(const ext2_volume_t *vol, u32 block, void *buf)
{
  const u32 sectors_per_block = vol->block_size / EXT2_SECTOR_SIZE;
  const u32 sector            = block * sectors_per_block;
  return vol_read_sectors(vol, sector, sectors_per_block, buf);
}

/**
 * @brief Write one ext2 block worth of bytes from @p buf.
 *
 * Counterpart of @ref vol_read_block; same single-source-of-truth rationale
 * for the block-to-sector translation.
 *
 * @param vol    Target volume.
 * @param block  Ext2 block number.
 * @param buf    Source buffer (≥ @c vol->block_size bytes).
 * @return Bytes transferred on success, negative errno on I/O failure.
 */
i64 vol_write_block(const ext2_volume_t *vol, u32 block, const void *buf)
{
  const u32 sectors_per_block = vol->block_size / EXT2_SECTOR_SIZE;
  const u32 sector            = block * sectors_per_block;
  return vol_write_sectors(vol, sector, sectors_per_block, buf);
}

/**
 * @brief Persist the in-memory superblock back to disk.
 *
 * Read-modify-write because the superblock lives inside a 1 KiB block that
 * may have other bytes we don't model; reading first preserves those.
 *
 * @param vol  Volume with the mutated @c vol->sb.
 * @return 0 on success, negative errno on I/O failure.
 */
static i64 write_superblock(ext2_volume_t *vol)
{
  u8 buf[EXT2_MIN_BLOCK_SIZE];
  if(vol_read_sectors(vol, 2, 2, buf) < 0)
    return -EIO;
  kmemcpy(buf, &vol->sb, sizeof(ext2_superblock_t));
  if(vol_write_sectors(vol, 2, 2, buf) < 0)
    return -EIO;
  return 0;
}

/**
 * @brief Persist the group descriptor table back to disk.
 *
 * Walks block-by-block so a single huge GDT split across many blocks doesn't
 * need a contiguous kmalloc the size of the whole table — only one block's
 * worth of scratch.
 *
 * @param vol  Volume with the mutated @c vol->groups.
 * @return 0 on success, negative errno on I/O failure or OOM.
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

/**
 * @brief Persist superblock + GDT in a single call.
 *
 * Called by every mutating op (mkdir, unlink, write…) so a crash between
 * the in-memory mutation and the next flush only loses content, never the
 * structural metadata that points at it.
 *
 * @param vol  Volume whose in-memory metadata is dirty.
 * @return 0 on success, the first negative errno encountered otherwise.
 */
i64 flush_metadata(ext2_volume_t *vol)
{
  i64 ret = write_superblock(vol);
  if(ret < 0)
    return ret;
  return write_group_descriptors(vol);
}

/**
 * @brief Initialise the ext2 driver: clear the volume + file pools and
 * register the fs type with the VFS.
 *
 * @param dev  Default block device that subsequent @c mount("ext2") calls
 *             will pick up when no @c source is supplied.
 */
void ext2_init(const blockdev_t *dev)
{
  g_default_dev = dev;
  kzero(g_volumes, sizeof(g_volumes));
  kzero(g_files, sizeof(g_files));
  vfs_register_fs(&g_ext2_fstype);
}

/**
 * @brief Mount an ext2 partition: read the superblock, validate the magic,
 * load the group descriptor table into RAM, mark the slot live.
 *
 * @param dev            Block device backing the volume.
 * @param partition_lba  Partition start, in sectors.
 * @return Volume handle on success, NULL on slot exhaustion / bad magic / OOM.
 */
ext2_volume_t *ext2_mount(const blockdev_t *dev, u32 partition_lba)
{
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

  u8 sb_buf[EXT2_MIN_BLOCK_SIZE];
  if(dev->read(dev->ctx, partition_lba + 2, 2, sb_buf) < 0) {
    console_print("[EXT2] Failed to read superblock\n");
    return NULL;
  }

  ext2_superblock_t *sb = (ext2_superblock_t *)sb_buf;
  if(sb->s_magic != EXT2_MAGIC) {
    console_print("[EXT2] Invalid magic number\n");
    return NULL;
  }

  vol->dev              = dev;
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
