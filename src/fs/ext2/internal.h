/**
 * @file src/fs/ext2/internal.h
 * @brief Shared state, sizing macros, and private helpers for the ext2
 *        module split.
 *
 * Not part of any public/UAPI surface — purely the contract between the
 * source files under @c src/fs/ext2/. Public consumers go through
 * @c <alcor2/fs/ext2.h> instead. Splitting the original 2.7 KLOC monolith
 * required a single source of truth for the shared volume / file pools and
 * the cross-file helpers; this header is that source.
 */

#ifndef ALCOR2_FS_EXT2_INTERNAL_H
#define ALCOR2_FS_EXT2_INTERNAL_H

#include <alcor2/fs/blockdev.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/types.h>

/** @brief Maximum mounted ext2 volumes. Picked at 4 because each volume holds
 * a copy of the superblock + group descriptor table; 4 keeps the static state
 * around 16 KiB while still allowing root + a few mountpoints. */
#define EXT2_MAX_VOLUMES 4

/** @brief Maximum concurrent open files. 256 covers a typical shell session
 * plus background daemons without ever evicting a handle. */
#define EXT2_MAX_FILES 256

/** @brief Size of the block scratch buffer pool. 8 is enough to overlap the
 * deepest indirect-block walk (triple-indirect = 3 nested reads) without
 * touching @c kmalloc, plus a handful for concurrent metadata I/O. */
#define EXT2_BLOCK_CACHE_SIZE 8

/** @brief Largest block size the scratch pool supports. ext2 allows up to
 * 64 KiB blocks; we cap at 4 KiB because anything larger requires reworking
 * the static pool sizing and no shipped image uses one. */
#define EXT2_MAX_BLOCK_SIZE 4096

/** @brief Max contiguous blocks coalesced in a single @ref ext2_read pass
 * (16 × 4 KiB = 64 KiB). Picks the sweet spot between ATA DMA throughput
 * and the kernel stack scratch needed for the run buffer. */
#define EXT2_READ_RUN_MAX 16

/** @brief Mounted-volume pool. Defined in @c super.c. */
extern ext2_volume_t g_volumes[EXT2_MAX_VOLUMES];

/** @brief Default block device — first to register, used by mount("ext2")
 * when no @c source path is supplied. Defined in @c super.c. */
extern const blockdev_t *g_default_dev;

/** @brief Open-file pool — every @ref ext2_file_t handed back to userspace
 * lives here. Defined in @c file.c. */
extern ext2_file_t g_files[EXT2_MAX_FILES];

/**
 * @brief Acquire a single-block scratch buffer from the static pool.
 *
 * Pool entries are @ref EXT2_MAX_BLOCK_SIZE bytes each so any block size we
 * support fits without reallocation. Falls back to @c kmalloc when the pool
 * is exhausted or @p size exceeds the pool entry size — the caller's
 * @ref cache_put_block knows which side to free.
 *
 * @param size  Required buffer size in bytes.
 * @return Pointer to a usable buffer (pool or heap), or @c NULL on OOM.
 */
u8 *cache_get_block(u32 size);

/**
 * @brief Release a buffer previously returned by @ref cache_get_block.
 *
 * Detects pool vs heap origin by pointer identity against the pool entries,
 * so callers don't need to track which path they came from.
 *
 * @param buf  Buffer to release.
 */
void cache_put_block(u8 *buf);

/**
 * @brief Read a sector run from a volume.
 *
 * Inlined here so the multi-block DMA fast path in @ref ext2_read can use it
 * without paying for an extra function call per @c EXT2_READ_RUN_MAX-sized
 * chunk. Adds the partition LBA so callers stay in partition-local sector
 * coordinates.
 *
 * @param vol     Source volume.
 * @param sector  Sector offset within the partition.
 * @param count   Sector count.
 * @param buf     Destination buffer.
 * @return Bytes read on success, negative errno on I/O failure.
 */
static inline i64
    vol_read_sectors(const ext2_volume_t *vol, u32 sector, u32 count, void *buf)
{
  return vol->dev->read(vol->dev->ctx, vol->partition_lba + sector, count, buf);
}

/**
 * @brief Companion of @ref vol_read_sectors for writes.
 *
 * @param vol     Target volume.
 * @param sector  Sector offset within the partition.
 * @param count   Sector count.
 * @param buf     Source buffer.
 * @return Bytes written on success, negative errno on I/O failure.
 */
static inline i64 vol_write_sectors(
    const ext2_volume_t *vol, u32 sector, u32 count, const void *buf
)
{
  return vol->dev->write(
      vol->dev->ctx, vol->partition_lba + sector, count, buf
  );
}

/**
 * @brief Read one filesystem block (vol-relative) into @p buf.
 *
 * Defined in @c super.c. Bridges the byte-addressed block layer to the
 * blockdev's sector-based read by multiplying by @c sectors_per_block.
 *
 * @param vol    Source volume.
 * @param block  Block number (0 = superblock area).
 * @param buf    Destination buffer of @c vol->block_size bytes.
 * @return 0 on success, negative errno on I/O failure.
 */
i64 vol_read_block(const ext2_volume_t *vol, u32 block, void *buf);

/**
 * @brief Companion of @ref vol_read_block for writes.
 *
 * @param vol    Target volume.
 * @param block  Block number.
 * @param buf    Source buffer of @c vol->block_size bytes.
 * @return 0 on success, negative errno on I/O failure.
 */
i64 vol_write_block(const ext2_volume_t *vol, u32 block, const void *buf);

/**
 * @brief Flush the in-memory superblock + group descriptor table back to disk.
 *
 * Called after any allocation / free that mutates metadata so a power loss
 * doesn't leave the on-disk view diverged from the live state. Defined in
 * @c super.c.
 *
 * @param vol  Volume to flush.
 * @return 0 on success, negative errno on I/O failure.
 */
i64 flush_metadata(ext2_volume_t *vol);

/** @brief @c fs_ops_t / @c fs_type_t registration record for ext2. Defined in
 * @c ops.c; @ref ext2_init publishes it via @c vfs_register_fs. */
extern const fs_type_t g_ext2_fstype;

/**
 * @brief Resolve a file-block index into its on-disk block number.
 *
 * Walks direct → single → double → triple indirect blocks as needed. Defined
 * in @c blockmap.c. Returns 0 (the disk's "no block" marker for ext2) when
 * the index falls in a hole.
 *
 * @param vol         Source volume.
 * @param inode       File inode (read-only — no allocation happens here).
 * @param file_block  Zero-based file block index.
 * @return On-disk block number, or 0 when the index is unallocated.
 */
u32 get_block_num(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block
);

/**
 * @brief Ensure a block is allocated for file-block index @p file_block.
 *
 * Allocates the data block, plus any indirect blocks along the way, then
 * patches them into @p inode 's block array. Defined in @c blockmap.c.
 *
 * @param vol              Target volume.
 * @param inode            File inode; mutated in place when new blocks are
 *                         added.
 * @param file_block       Zero-based file block index.
 * @param preferred_group  Group hint passed through to @ref alloc_block —
 *                         pass the inode's group to keep allocations local.
 * @return On-disk block number, or 0 on allocation failure.
 */
u32 alloc_file_block(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 file_block, u32 preferred_group
);

/**
 * @brief Free every block referenced by @p inode, including indirect blocks
 *        themselves.
 *
 * Walks direct + indirect ranges, calling @ref free_block on each. Resets
 * @p inode 's block array and size/blocks counters so the caller can persist
 * the truncated inode.
 *
 * @param vol    Source volume.
 * @param inode  File inode; mutated in place.
 * @return 0 on success, negative errno on I/O failure.
 */
i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode);

/**
 * @brief Read inode @p ino from disk into @p inode.
 *
 * Defined in @c inode.c. Locates the inode by walking
 * @c group → @c inode_table → @c offset; the block read goes through
 * @ref vol_read_block.
 *
 * @param vol    Source volume.
 * @param ino    1-based inode number.
 * @param inode  Destination inode struct.
 * @return 0 on success, negative errno on invalid @p ino or I/O failure.
 */
i64 read_inode(const ext2_volume_t *vol, u32 ino, ext2_inode_t *inode);

/**
 * @brief Allocate one block on @p vol, biased toward @p preferred_group.
 *
 * Tries @p preferred_group first to keep allocations spatially local (good
 * for the indirect-block walker); falls back to a linear scan over the rest.
 * Defined in @c alloc.c.
 *
 * @param vol              Target volume.
 * @param preferred_group  Group to consult first.
 * @return Block number on success, 0 when every group is full.
 */
u32 alloc_block(ext2_volume_t *vol, u32 preferred_group);

/**
 * @brief Mark @p block free on @p vol.
 *
 * @param vol    Source volume.
 * @param block  Block number to free.
 * @return 0 on success, negative errno on out-of-range / I/O failure.
 */
i64 free_block(ext2_volume_t *vol, u32 block);

/**
 * @brief Allocate one inode on @p vol, biased toward @p preferred_group.
 *
 * @param vol              Target volume.
 * @param preferred_group  Group to consult first.
 * @param is_dir           When true, also bumps @c bg_used_dirs_count.
 * @return Inode number on success, 0 when every group is full.
 */
u32 alloc_inode(ext2_volume_t *vol, u32 preferred_group, bool is_dir);

/**
 * @brief Mark inode @p ino free on @p vol.
 *
 * @param vol     Source volume.
 * @param ino     1-based inode number.
 * @param is_dir  When true, decrements @c bg_used_dirs_count.
 * @return 0 on success, negative errno on out-of-range / I/O failure.
 */
i64 free_inode(ext2_volume_t *vol, u32 ino, bool is_dir);

/**
 * @brief Persist @p inode for inode number @p ino back to disk.
 *
 * Read-modify-write: the inode lives inside a block that also holds other
 * inodes, so we need to preserve them.
 *
 * @param vol    Target volume.
 * @param ino    1-based inode number.
 * @param inode  Inode struct to persist.
 * @return 0 on success, negative errno on invalid @p ino or I/O failure.
 */
i64 write_inode(const ext2_volume_t *vol, u32 ino, const ext2_inode_t *inode);

#endif /* ALCOR2_FS_EXT2_INTERNAL_H */
