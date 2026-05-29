/**
 * @file src/fs/ext2/dir_entry.c
 * @brief On-disk directory entry primitives — lookup, insert, remove,
 *        emptiness check.
 *
 * The on-disk directory format is a sequence of variable-length records
 * inside the directory's data blocks; every helper here walks that
 * structure with the same offset / @c rec_len arithmetic. Kept in one file
 * so the dirent layout knowledge stays in a single place and the higher-
 * level ops in @c dir.c only depend on the typed interface (find / add /
 * remove / is_empty), never on @c rec_len byte math.
 *
 * The per-block scanners (@ref dir_block_find_name, @ref
 * dir_block_remove_name, @ref dir_block_count_entries, @ref
 * dir_block_try_insert) own the inner dirent walk; the public functions
 * just iterate the directory's data blocks and delegate.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Byte alignment for an on-disk @c ext2_dirent_t record.
 *
 * ext2 stores dirents at 4-byte boundaries; @c rec_len is rounded up to
 * the next multiple. Named so the rounding math reads as alignment, not
 * as the literal @c 4 it derives from.
 */
#define EXT2_DIRENT_ALIGN 4u

/**
 * @brief Aligned on-disk size of a dirent with @p name_len bytes of name.
 *
 * @param name_len  Name length in bytes (excluding NUL — dirents are not
 *                  NUL-terminated).
 * @return Bytes the dirent occupies on disk, rounded up to
 *         @ref EXT2_DIRENT_ALIGN.
 */
static inline u32 dirent_aligned_len(u32 name_len)
{
  return ((u32)sizeof(ext2_dirent_t) + name_len + EXT2_DIRENT_ALIGN - 1u) &
         ~(EXT2_DIRENT_ALIGN - 1u);
}

/**
 * @brief Locate the dirent named @p name inside a single block buffer.
 *
 * Returns a mutable pointer so the remove path can splice without a
 * second scan; the find path treats it as read-only. Stops at the first
 * zero @c rec_len because that's how ext2 marks "no more dirents in this
 * block".
 *
 * @param block_buf  Loaded block contents.
 * @param block_size Block size in bytes.
 * @param name       Name to match (not NUL-terminated within the dirent).
 * @param name_len   Length of @p name.
 * @return Dirent pointer on hit, NULL if not in this block.
 */
static ext2_dirent_t *dir_block_find_name(
    u8 *block_buf, u32 block_size, const char *name, u32 name_len
)
{
  u32 off = 0;
  while(off < block_size) {
    ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + off);
    if(de->rec_len == 0)
      return NULL;
    if(de->inode != 0 && de->name_len == name_len &&
       kstrncmp(de->name, name, name_len) == 0)
      return de;
    off += de->rec_len;
  }
  return NULL;
}

/**
 * @brief Remove the dirent named @p name from a single block buffer.
 *
 * Two cases: first-in-block hits get @c inode @c = @c 0 (tombstone); any
 * later hit gets folded into the previous dirent's @c rec_len so the
 * dirent becomes unreachable via the walk but the block stays compact.
 *
 * @param block_buf  Loaded block contents (mutated on hit).
 * @param block_size Block size in bytes.
 * @param name       Name to remove.
 * @param name_len   Length of @p name.
 * @return @c true if the block was modified, @c false if @p name wasn't found.
 */
static bool dir_block_remove_name(
    u8 *block_buf, u32 block_size, const char *name, u32 name_len
)
{
  u32            off  = 0;
  ext2_dirent_t *prev = NULL;
  while(off < block_size) {
    ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + off);
    if(de->rec_len == 0)
      return false;
    if(de->inode != 0 && de->name_len == name_len &&
       kstrncmp(de->name, name, name_len) == 0) {
      if(prev)
        prev->rec_len += de->rec_len;
      else
        de->inode = 0;
      return true;
    }
    prev = de;
    off += de->rec_len;
  }
  return false;
}

/**
 * @brief Count non-./.. dirents in a single block buffer.
 *
 * @c . and @c .. are the structural entries every directory carries; the
 * emptiness check is interested only in user-visible children, so they
 * are filtered out here rather than at the caller.
 *
 * @param block_buf  Loaded block contents.
 * @param block_size Block size in bytes.
 * @return Number of in-use, non-./.. dirents.
 */
static u32 dir_block_count_entries(const u8 *block_buf, u32 block_size)
{
  u32 count = 0;
  u32 off   = 0;
  while(off < block_size) {
    const ext2_dirent_t *de = (const ext2_dirent_t *)(block_buf + off);
    if(de->rec_len == 0)
      break;
    if(de->inode != 0) {
      bool is_dot = (de->name_len == 1 && de->name[0] == '.');
      bool is_dotdot =
          (de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.');
      if(!is_dot && !is_dotdot)
        count++;
    }
    off += de->rec_len;
  }
  return count;
}

/**
 * @brief Try to insert a new dirent into a single block by splicing slack.
 *
 * ext2 packs dirents back-to-back with each carrying @c rec_len = "bytes
 * until next dirent (or end of block)". Slack at the end of an existing
 * dirent is reusable: shrink the predecessor's @c rec_len to its actual
 * size and lay the new dirent in the recovered space.
 *
 * @param block_buf  Loaded block contents (mutated on success).
 * @param block_size Block size in bytes.
 * @param name       Entry name.
 * @param name_len   Length of @p name.
 * @param inode_num  Target inode number.
 * @param file_type  EXT2_FT_* file type.
 * @return @c true if the block now contains the new entry, @c false if no
 *         slack big enough was found.
 */
static bool dir_block_try_insert(
    u8 *block_buf, u32 block_size, const char *name, u32 name_len,
    u32 inode_num, u8 file_type
)
{
  u32 needed = dirent_aligned_len(name_len);
  u32 off    = 0;
  while(off < block_size) {
    ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + off);
    if(de->rec_len == 0)
      return false;
    u32 actual     = dirent_aligned_len(de->name_len);
    u32 free_space = de->rec_len - actual;
    if(free_space >= needed) {
      u32 new_rec_len       = de->rec_len - actual;
      de->rec_len           = (u16)actual;
      ext2_dirent_t *new_de = (ext2_dirent_t *)(block_buf + off + actual);
      new_de->inode         = inode_num;
      new_de->rec_len       = (u16)new_rec_len;
      new_de->name_len      = (u8)name_len;
      new_de->file_type     = file_type;
      kmemcpy(new_de->name, name, name_len);
      return true;
    }
    off += de->rec_len;
  }
  return false;
}

/**
 * @brief Lay a single dirent in a freshly allocated, zeroed block.
 *
 * Used when @ref dir_block_try_insert can't find slack and the caller
 * grows the directory by one block. The new dirent's @c rec_len covers
 * the whole block so subsequent inserts have the full block as slack.
 *
 * @param block_buf  Block buffer (zeroed then populated).
 * @param block_size Block size in bytes.
 * @param name       Entry name.
 * @param name_len   Length of @p name.
 * @param inode_num  Target inode number.
 * @param file_type  EXT2_FT_* file type.
 */
static void dir_init_first_entry(
    u8 *block_buf, u32 block_size, const char *name, u32 name_len,
    u32 inode_num, u8 file_type
)
{
  kzero(block_buf, block_size);
  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode         = inode_num;
  de->rec_len       = (u16)block_size;
  de->name_len      = (u8)name_len;
  de->file_type     = file_type;
  kmemcpy(de->name, name, name_len);
}

/**
 * @brief Find a directory entry by name.
 *
 * Walks the directory's data blocks; the per-block scan is done by
 * @ref dir_block_find_name so the dirent layout stays in one place.
 * Holes (sparse blocks) are skipped silently — a hole means "no entries
 * in this slot".
 *
 * @note Body is 30 LOC: the per-block work is delegated, but the kmalloc
 *       lifecycle + per-iteration error cleanup needs to bracket the
 *       outer loop. Extracting that into a callback iterator would cost
 *       more clarity than it saves.
 *
 * @param vol        Volume.
 * @param dir_inode  Directory inode.
 * @param name       Entry name to find.
 * @param out_ino    Output inode number if found.
 * @param out_type   Output file type if found.
 * @return 0 on success, -ENOENT if not found, -ENOMEM / -EIO on failure.
 */
i64 dir_find_entry(
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

  for(u32 off = 0; off < dir_size; off += block_size) {
    u32 block_num = get_block_num(vol, dir_inode, off / block_size);
    if(block_num == 0)
      continue;
    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return -EIO;
    }
    ext2_dirent_t *de =
        dir_block_find_name(block_buf, block_size, name, name_len);
    if(de) {
      *out_ino  = de->inode;
      *out_type = de->file_type;
      kfree(block_buf);
      return 0;
    }
  }

  kfree(block_buf);
  return -ENOENT;
}

/**
 * @brief Try to splice the new entry into any existing block's slack.
 *
 * Sweep over every allocated block in the directory; on the first block
 * with enough slack, @ref dir_block_try_insert mutates it in place and
 * we persist with @ref vol_write_block. -ENOENT is the "nowhere to put
 * it" sentinel that tells @ref dir_add_entry to fall through to growth.
 *
 * @param vol        Volume.
 * @param dir_inode  Directory inode.
 * @param name       Entry name.
 * @param name_len   Length of @p name.
 * @param inode_num  Target inode number.
 * @param file_type  EXT2_FT_* file type.
 * @param block_buf  Caller-owned scratch buffer of @c vol->block_size bytes.
 * @return 0 on insert, -ENOENT if no block had enough slack, -EIO on write
 *         failure.
 */
static i64 dir_try_insert_into_existing(
    const ext2_volume_t *vol, const ext2_inode_t *dir_inode, const char *name,
    u32 name_len, u32 inode_num, u8 file_type, u8 *block_buf
)
{
  u32 block_size = vol->block_size;
  u32 dir_blocks = (dir_inode->i_size + block_size - 1) / block_size;
  for(u32 b = 0; b < dir_blocks; b++) {
    u32 block_num = get_block_num(vol, dir_inode, b);
    if(block_num == 0)
      continue;
    if(vol_read_block(vol, block_num, block_buf) < 0)
      continue;
    if(dir_block_try_insert(
           block_buf, block_size, name, name_len, inode_num, file_type
       ))
      return (vol_write_block(vol, block_num, block_buf) < 0) ? -EIO : 0;
  }
  return -ENOENT;
}

/**
 * @brief Grow the directory by one block, seeded with the new entry.
 *
 * Allocates a fresh data block via @ref alloc_file_block, lays down the
 * single dirent via @ref dir_init_first_entry, persists the block, and
 * publishes the larger @c i_size by rewriting the inode.
 *
 * @param vol        Volume.
 * @param dir_ino    Directory inode number.
 * @param dir_inode  Directory inode (mutated: @c i_size grows by one block).
 * @param name       Entry name.
 * @param name_len   Length of @p name.
 * @param inode_num  Target inode number.
 * @param file_type  EXT2_FT_* file type.
 * @param block_buf  Caller-owned scratch buffer of @c vol->block_size bytes.
 * @return 0 on success, -ENOSPC / -EIO on failure.
 */
static i64 dir_grow_with_entry(
    ext2_volume_t *vol, u32 dir_ino, ext2_inode_t *dir_inode, const char *name,
    u32 name_len, u32 inode_num, u8 file_type, u8 *block_buf
)
{
  u32 block_size    = vol->block_size;
  u32 dir_blocks    = (dir_inode->i_size + block_size - 1) / block_size;
  u32 preferred_grp = (dir_ino - 1) / vol->inodes_per_group;
  u32 new_block = alloc_file_block(vol, dir_inode, dir_blocks, preferred_grp);
  if(new_block == 0)
    return -ENOSPC;
  dir_init_first_entry(
      block_buf, block_size, name, name_len, inode_num, file_type
  );
  if(vol_write_block(vol, new_block, block_buf) < 0)
    return -EIO;
  dir_inode->i_size += block_size;
  return (write_inode(vol, dir_ino, dir_inode) < 0) ? -EIO : 0;
}

/**
 * @brief Add a directory entry.
 *
 * Two-phase: try @ref dir_try_insert_into_existing first; on -ENOENT,
 * fall through to @ref dir_grow_with_entry. The scratch buffer is owned
 * here so both helpers reuse the same allocation.
 *
 * @param vol        Volume.
 * @param dir_ino    Directory inode number.
 * @param dir_inode  Directory inode (mutated if the directory grows).
 * @param name       Entry name.
 * @param inode_num  Inode number for new entry.
 * @param file_type  File type (EXT2_FT_*).
 * @return 0 on success, -ENOMEM / -ENOSPC / -EIO on failure.
 */
i64 dir_add_entry(
    ext2_volume_t *vol, u32 dir_ino, ext2_inode_t *dir_inode, const char *name,
    u32 inode_num, u8 file_type
)
{
  u32 name_len  = kstrlen(name);
  u8 *block_buf = kmalloc(vol->block_size);
  if(!block_buf)
    return -ENOMEM;
  i64 ret = dir_try_insert_into_existing(
      vol, dir_inode, name, name_len, inode_num, file_type, block_buf
  );
  if(ret == -ENOENT)
    ret = dir_grow_with_entry(
        vol, dir_ino, dir_inode, name, name_len, inode_num, file_type, block_buf
    );
  kfree(block_buf);
  return ret;
}

/**
 * @brief Remove a directory entry.
 *
 * Per-block scan via @ref dir_block_remove_name; on hit, the block is
 * written back since the scanner mutated it in place (either tombstoned
 * the head dirent or merged into the previous one).
 *
 * @note Body is 27 LOC — same kmalloc-bracketed outer loop as
 *       @ref dir_find_entry, kept symmetric on purpose so the two read
 *       identically next to each other.
 *
 * @param vol        Volume.
 * @param dir_inode  Directory inode.
 * @param name       Entry name to remove.
 * @return 0 on success, -ENOENT if absent, -ENOMEM / -EIO on failure.
 */
i64 dir_remove_entry(
    const ext2_volume_t *vol, const ext2_inode_t *dir_inode, const char *name
)
{
  u32 name_len   = kstrlen(name);
  u32 dir_size   = dir_inode->i_size;
  u32 block_size = vol->block_size;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return -ENOMEM;

  for(u32 off = 0; off < dir_size; off += block_size) {
    u32 block_num = get_block_num(vol, dir_inode, off / block_size);
    if(block_num == 0)
      continue;
    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return -EIO;
    }
    if(dir_block_remove_name(block_buf, block_size, name, name_len)) {
      i64 ret = (vol_write_block(vol, block_num, block_buf) < 0) ? -EIO : 0;
      kfree(block_buf);
      return ret;
    }
  }

  kfree(block_buf);
  return -ENOENT;
}

/**
 * @brief Check if a directory is empty.
 *
 * Sums non-./.. counts across blocks via @ref dir_block_count_entries.
 * Short-circuits once any block produces a non-zero count so a large
 * directory with one user entry near the start exits early.
 *
 * @param vol        Volume.
 * @param dir_inode  Directory inode.
 * @return true if only . and .. are present, false otherwise (including
 *         on allocation / I/O failure — safer to refuse rmdir than to
 *         delete a directory we couldn't fully inspect).
 */
bool dir_is_empty(const ext2_volume_t *vol, const ext2_inode_t *dir_inode)
{
  u32 dir_size   = dir_inode->i_size;
  u32 block_size = vol->block_size;

  u8 *block_buf = kmalloc(block_size);
  if(!block_buf)
    return false;

  for(u32 off = 0; off < dir_size; off += block_size) {
    u32 block_num = get_block_num(vol, dir_inode, off / block_size);
    if(block_num == 0)
      continue;
    if(vol_read_block(vol, block_num, block_buf) < 0) {
      kfree(block_buf);
      return false;
    }
    if(dir_block_count_entries(block_buf, block_size) > 0) {
      kfree(block_buf);
      return false;
    }
  }

  kfree(block_buf);
  return true;
}
