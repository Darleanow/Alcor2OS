/**
 * @file src/fs/ext2/blockmap.c
 * @brief Direct + indirect block resolution and reservation for ext2.
 *
 * ext2 spreads a file's blocks across three regions: 12 direct entries in the
 * inode, then a single-indirect block, then a double-indirect block, then a
 * triple-indirect block. @ref get_block_num walks them read-only; @ref
 * alloc_file_block grows the indirect tree on demand; @ref free_inode_blocks
 * undoes both passes. Kept in one file because the three operations share
 * the same level/range arithmetic and getting it wrong in one mirrors it in
 * the others.
 *
 * Each level reuses the same primitives so the per-level code stays a thin
 * recipe rather than an inlined copy of the alloc / zero / write-back dance.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Number of u32 block-pointer slots per indirect block.
 *
 * Single source of truth for the indirect-tree fan-out; every level math
 * (direct→single→double→triple) is derived from this.
 *
 * @param vol  Volume (block size is per-mount).
 * @return Fan-out of one indirect block.
 */
static inline u32 ptrs_per_block(const ext2_volume_t *vol)
{
  return vol->block_size / sizeof(u32);
}

/**
 * @brief Allocate a fresh block, zero it, and bill the inode for it.
 *
 * Used for both data and metadata (indirect) blocks: in both cases the
 * caller wants a known-zero block they own, and @c i_blocks must track
 * every block charged to the inode regardless of role.
 *
 * @param vol              Target volume.
 * @param preferred_group  Locality hint for @ref alloc_block.
 * @param inode            Inode whose @c i_blocks gets bumped on success.
 * @return Block number on success, 0 if @ref alloc_block returns 0.
 */
static u32 alloc_zeroed_block(
    ext2_volume_t *vol, u32 preferred_group, ext2_inode_t *inode
)
{
  u32 block = alloc_block(vol, preferred_group);
  if(block == 0)
    return 0;
  u8 *zero = kmalloc(vol->block_size);
  if(zero) {
    kzero(zero, vol->block_size);
    vol_write_block(vol, block, zero);
    kfree(zero);
  }
  inode->i_blocks += vol->block_size / EXT2_SECTOR_SIZE;
  return block;
}

/**
 * @brief Read slot @p slot_idx out of indirect block @p indirect_block.
 *
 * @p indirect_block @c == @c 0 means "this branch of the tree was never
 * allocated"; returning 0 there is the read-only counterpart of @ref
 * get_block_num's "hole" semantics. I/O failure also folds into 0 since
 * the read-only path has no error channel to bubble up.
 *
 * @param vol             Volume.
 * @param indirect_block  Block number of the indirect block, or 0 for hole.
 * @param slot_idx        Index inside the indirect block.
 * @return Slot value, or 0 on hole / OOM / I/O failure.
 */
static u32 read_indirect_slot(
    const ext2_volume_t *vol, u32 indirect_block, u32 slot_idx
)
{
  if(indirect_block == 0)
    return 0;
  u32 *buf = kmalloc(vol->block_size);
  if(!buf)
    return 0;
  if(vol_read_block(vol, indirect_block, buf) < 0) {
    kfree(buf);
    return 0;
  }
  u32 result = buf[slot_idx];
  kfree(buf);
  return result;
}

/**
 * @brief Resolve slot @p slot_idx of @p indirect_block; allocate it if hole.
 *
 * Read-modify-write of one indirect-block slot. On hole, allocates a fresh
 * zeroed child block via @ref alloc_zeroed_block, plugs it into the slot,
 * and writes the parent indirect block back. The same primitive serves
 * both "leaf data block" and "next-level indirect block" callers because
 * the on-disk layout treats them identically.
 *
 * @param vol              Volume.
 * @param inode            Inode being grown (forwarded to @ref
 * alloc_zeroed_block).
 * @param indirect_block   Parent indirect block (must already exist).
 * @param slot_idx         Slot inside the parent.
 * @param preferred_group  Locality hint.
 * @return Slot value (existing or freshly allocated), 0 on OOM / I/O fail.
 */
static u32 ensure_indirect_slot(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 indirect_block, u32 slot_idx,
    u32 preferred_group
)
{
  u32 *buf = kmalloc(vol->block_size);
  if(!buf)
    return 0;
  if(vol_read_block(vol, indirect_block, buf) < 0) {
    kfree(buf);
    return 0;
  }
  u32 child = buf[slot_idx];
  if(child == 0) {
    child = alloc_zeroed_block(vol, preferred_group, inode);
    if(child == 0) {
      kfree(buf);
      return 0;
    }
    buf[slot_idx] = child;
    vol_write_block(vol, indirect_block, buf);
  }
  kfree(buf);
  return child;
}

/**
 * @brief Resolve top-level inode slot @p slot_idx; allocate it if hole.
 *
 * Analogue of @ref ensure_indirect_slot for the @c i_block[] array, which
 * lives inside the inode itself rather than in a separately-stored
 * indirect block. No read-modify-write because the inode is the caller's
 * own in-memory copy.
 *
 * @param vol              Volume.
 * @param inode            Inode (mutated in place).
 * @param slot_idx         Index into @c inode->i_block[].
 * @param preferred_group  Locality hint.
 * @return Block number (existing or freshly allocated), 0 on alloc fail.
 */
static u32 ensure_inode_slot(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 slot_idx, u32 preferred_group
)
{
  if(inode->i_block[slot_idx] == 0)
    inode->i_block[slot_idx] = alloc_zeroed_block(vol, preferred_group, inode);
  return inode->i_block[slot_idx];
}

/**
 * @brief Resolve a file-relative block index to an on-disk block number.
 *
 * Four ranges, mapped left-to-right: 12 direct slots → single → double →
 * triple. The @c file_block subtraction at each boundary lets the same
 * range check (@c file_block @c < @c ppb) repeat at each level instead of
 * accumulating offsets.
 *
 * @param vol         Source volume.
 * @param inode       Inode owning the file.
 * @param file_block  File-relative block index (0 = first block).
 * @return On-disk block number, or 0 when the block is a hole.
 */
u32 get_block_num(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block
)
{
  const u32 ppb = ptrs_per_block(vol);
  if(file_block < EXT2_NDIR_BLOCKS)
    return inode->i_block[file_block];
  file_block -= EXT2_NDIR_BLOCKS;
  if(file_block < ppb)
    return read_indirect_slot(vol, inode->i_block[EXT2_IND_BLOCK], file_block);
  file_block -= ppb;
  if(file_block < ppb * ppb) {
    u32 ind = read_indirect_slot(
        vol, inode->i_block[EXT2_DIND_BLOCK], file_block / ppb
    );
    return read_indirect_slot(vol, ind, file_block % ppb);
  }
  file_block -= ppb * ppb;
  u32 dind = read_indirect_slot(
      vol, inode->i_block[EXT2_TIND_BLOCK], file_block / (ppb * ppb)
  );
  u32 rem = file_block % (ppb * ppb);
  u32 ind = read_indirect_slot(vol, dind, rem / ppb);
  return read_indirect_slot(vol, ind, rem % ppb);
}

/**
 * @brief Reserve the block at file-relative index @p file_block.
 *
 * Mirrors @ref get_block_num's level structure but each step uses the
 * @c ensure_* helpers so a hole at any level along the path becomes a
 * freshly allocated zeroed block. Early-out on every alloc failure so a
 * partial alloc doesn't strand metadata blocks the inode now thinks it
 * owns but can't reach.
 *
 * @note Body exceeds the 25-LOC soft cap (four mutually exclusive ranges:
 *       direct, single, double, triple). This is the table-walk exception
 *       the rules allow: the four blocks mirror the on-disk indirect-tree
 *       layout 1:1, and folding them into a loop over a level table loses
 *       that direct correspondence.
 *
 * @param vol              Target volume.
 * @param inode            Inode being grown.
 * @param file_block       File-relative block index.
 * @param preferred_group  Locality hint for new blocks.
 * @return Block number on success, 0 if any underlying alloc failed.
 */
u32 alloc_file_block(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 file_block, u32 preferred_group
)
{
  const u32 ppb = ptrs_per_block(vol);
  if(file_block < EXT2_NDIR_BLOCKS)
    return ensure_inode_slot(vol, inode, file_block, preferred_group);
  file_block -= EXT2_NDIR_BLOCKS;
  if(file_block < ppb) {
    u32 ind = ensure_inode_slot(vol, inode, EXT2_IND_BLOCK, preferred_group);
    return ind ? ensure_indirect_slot(
                     vol, inode, ind, file_block, preferred_group
                 )
               : 0;
  }
  file_block -= ppb;
  if(file_block < ppb * ppb) {
    u32 dind = ensure_inode_slot(vol, inode, EXT2_DIND_BLOCK, preferred_group);
    if(!dind)
      return 0;
    u32 ind = ensure_indirect_slot(
        vol, inode, dind, file_block / ppb, preferred_group
    );
    return ind ? ensure_indirect_slot(
                     vol, inode, ind, file_block % ppb, preferred_group
                 )
               : 0;
  }
  file_block -= ppb * ppb;
  u32 tind = ensure_inode_slot(vol, inode, EXT2_TIND_BLOCK, preferred_group);
  if(!tind)
    return 0;
  u32 dind = ensure_indirect_slot(
      vol, inode, tind, file_block / (ppb * ppb), preferred_group
  );
  if(!dind)
    return 0;
  u32 rem = file_block % (ppb * ppb);
  u32 ind = ensure_indirect_slot(vol, inode, dind, rem / ppb, preferred_group);
  return ind ? ensure_indirect_slot(vol, inode, ind, rem % ppb, preferred_group)
             : 0;
}

/**
 * @brief Recursively free an indirect-block subtree.
 *
 * @p depth selects how many indirection levels remain under @p block_num:
 * 0 = the children are leaf data blocks, 1 = single-indirect children,
 * 2 = double-indirect children. Folding the three "free the indirect
 * tree" variants into one recursive walk drops the linear repetition that
 * dominated the original @ref free_inode_blocks.
 *
 * @param vol        Volume.
 * @param block_num  Block to free (skipped if 0).
 * @param depth      Levels of indirection remaining (0..2).
 */
static void free_indirect_subtree(ext2_volume_t *vol, u32 block_num, u32 depth)
{
  if(block_num == 0)
    return;
  u32 *buf = kmalloc(vol->block_size);
  if(buf && vol_read_block(vol, block_num, buf) == 0) {
    u32 ppb = ptrs_per_block(vol);
    for(u32 i = 0; i < ppb; i++) {
      if(buf[i] == 0)
        continue;
      if(depth == 0)
        free_block(vol, buf[i]);
      else
        free_indirect_subtree(vol, buf[i], depth - 1);
    }
  }
  if(buf)
    kfree(buf);
  free_block(vol, block_num);
}

/**
 * @brief Free every block referenced by @p inode and reset its counters.
 *
 * Walks the three indirect roots via @ref free_indirect_subtree and the
 * 12 direct entries inline. Clears @c i_block[] entries as it goes so
 * a partial failure (mid-free crash) leaves the inode pointing at fewer
 * stale blocks rather than dangling references.
 *
 * @param vol    Volume.
 * @param inode  Inode (mutated in place).
 * @return Always 0; preserved for callers that fold this into an errno chain.
 */
i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode)
{
  for(u32 i = 0; i < EXT2_NDIR_BLOCKS; i++) {
    if(inode->i_block[i]) {
      free_block(vol, inode->i_block[i]);
      inode->i_block[i] = 0;
    }
  }
  free_indirect_subtree(vol, inode->i_block[EXT2_IND_BLOCK], 0);
  inode->i_block[EXT2_IND_BLOCK] = 0;
  free_indirect_subtree(vol, inode->i_block[EXT2_DIND_BLOCK], 1);
  inode->i_block[EXT2_DIND_BLOCK] = 0;
  free_indirect_subtree(vol, inode->i_block[EXT2_TIND_BLOCK], 2);
  inode->i_block[EXT2_TIND_BLOCK] = 0;
  inode->i_blocks                 = 0;
  return 0;
}
