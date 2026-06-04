/**
 * @file src/fs/ext2/dir.c
 * @brief High-level directory operations — readdir, mkdir, unlink, rmdir.
 *
 * Sits on top of the on-disk dirent primitives in @c dir_entry.c
 * (@c dir_find_entry / @c dir_add_entry / @c dir_remove_entry /
 * @c dir_is_empty) and the path/inode/block-allocation layers. The
 * separation keeps the rec_len byte arithmetic in one file so the VFS-
 * facing ops here read as policy (link-count rules, free-on-zero-links,
 * empty-on-rmdir) rather than dirent layout details.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/** @brief Default Unix mode for new directories — drwxr-xr-x. */
#define EXT2_DEFAULT_DIR_MODE 0755

/**
 * @brief On-disk size of a "." dirent: 8-byte header + 1 name byte aligned
 *        to @ref EXT2_DIRENT_ALIGN (4 bytes).
 *
 * Named so the @c rec_len math in @ref seed_dot_dotdot reads as "size of
 * the dot entry" rather than the literal @c 12 it computes to.
 */
#define EXT2_DOT_DIRENT_LEN 12u

/**
 * @brief Fill @p entry with the public-facing view of a dirent.
 *
 * Copies the name (truncating to @ref EXT2_NAME_MAX), type, inode number,
 * then resolves the target inode to surface its size — the size is a
 * read-only summary, not authoritative state, so a stale-on-disk inode
 * silently produces size=0 rather than failing the readdir.
 *
 * @param vol    Volume.
 * @param de     Source dirent.
 * @param entry  Output entry.
 */
static void fill_entry_from_dirent(
    const ext2_volume_t *vol, const ext2_dirent_t *de, ext2_entry_t *entry
)
{
  u32   name_len = de->name_len;
  char *dst      = (char *)entry->name;
  kmemcpy(dst, de->name, name_len);
  dst[name_len]    = '\0';
  entry->inode     = de->inode;
  entry->file_type = de->file_type;

  ext2_inode_t file_inode;
  entry->size =
      (read_inode(vol, de->inode, &file_inode) == 0) ? file_inode.i_size : 0;
}

/**
 * @brief Read the entry at @p index from an open directory.
 *
 * Walks the directory's data blocks from the start each call, counting
 * non-deleted entries until it reaches @p index. Linear scan is the
 * correct choice here: dir entries are variable-length, so a "seek to N"
 * is meaningless on disk; the only authoritative way to find entry N is
 * to walk N entries. Callers (readdir loops) just increment @p index.
 *
 * @param dir    Open directory handle.
 * @param index  Zero-based entry index to fetch.
 * @param entry  Output entry structure.
 * @return 1 if an entry was read, 0 at end, or negative errno on error.
 */
i64 ext2_readdir(ext2_file_t *dir, u64 index, ext2_entry_t *entry)
{
  if(!dir || !dir->in_use || !dir->is_dir)
    return -EINVAL;

  const ext2_volume_t *vol        = dir->vol;
  u32                  block_size = vol->block_size;
  u8                  *block_buf  = cache_get_block(block_size);
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
        fill_entry_from_dirent(vol, de, entry);
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
 * @brief Initialise the in-memory new-directory inode.
 *
 * @c i_links_count starts at 2 because both the parent's entry and the
 * fresh @c . dirent count as links to the directory.
 *
 * @param inode       Inode (zeroed and then populated).
 * @param block_size  Block size — the seed block is one full block.
 */
static void init_directory_inode(ext2_inode_t *inode, u32 block_size)
{
  kzero(inode, sizeof(*inode));
  inode->i_mode        = EXT2_S_IFDIR | EXT2_DEFAULT_DIR_MODE;
  inode->i_size        = block_size;
  inode->i_links_count = 2;
}

/**
 * @brief Populate @p block_buf with the @c . and @c .. dirents.
 *
 * The @c . dirent is exactly @ref EXT2_DOT_DIRENT_LEN bytes; the @c ..
 * dirent fills the remainder of the block so subsequent inserts have the
 * whole tail as slack.
 *
 * @param block_buf   Buffer (zeroed then populated).
 * @param block_size  Block size.
 * @param self_ino    Inode number of the new directory (for @c .).
 * @param parent_ino  Inode number of the parent (for @c ..).
 */
static void
    seed_dot_dotdot(u8 *block_buf, u32 block_size, u32 self_ino, u32 parent_ino)
{
  kzero(block_buf, block_size);
  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode         = self_ino;
  de->rec_len       = EXT2_DOT_DIRENT_LEN;
  de->name_len      = 1;
  de->file_type     = EXT2_FT_DIR;
  de->name[0]       = '.';

  de            = (ext2_dirent_t *)(block_buf + EXT2_DOT_DIRENT_LEN);
  de->inode     = parent_ino;
  de->rec_len   = (u16)(block_size - EXT2_DOT_DIRENT_LEN);
  de->name_len  = 2;
  de->file_type = EXT2_FT_DIR;
  de->name[0]   = '.';
  de->name[1]   = '.';
}

/**
 * @brief Allocate the first data block, seed it with ./.., persist it,
 *        and wire it into @p inode.
 *
 * Owns the buffer / block-alloc lifecycle so @ref ext2_mkdir reads as a
 * linear policy chain rather than carrying the alloc-rollback in line.
 *
 * @param vol             Volume.
 * @param self_ino        Inode number of the new directory (for @c .).
 * @param inode           Directory inode being populated.
 * @param parent_ino      Parent inode number (for @c ..).
 * @param preferred_grp   Locality hint.
 * @return 0 on success, -ENOSPC / -ENOMEM / -EIO on failure.
 */
static i64 mkdir_seed_first_block(
    ext2_volume_t *vol, u32 self_ino, ext2_inode_t *inode, u32 parent_ino,
    u32 preferred_grp
)
{
  u32 first_block = alloc_block(vol, preferred_grp);
  if(first_block == 0)
    return -ENOSPC;

  u8 *block_buf = kmalloc(vol->block_size);
  if(!block_buf) {
    free_block(vol, first_block);
    return -ENOMEM;
  }
  seed_dot_dotdot(block_buf, vol->block_size, self_ino, parent_ino);
  i64 wret = vol_write_block(vol, first_block, block_buf);
  kfree(block_buf);
  i64 ret = wret < 0 ? -EIO : 0;
  if(ret < 0) {
    free_block(vol, first_block);
    return ret;
  }

  inode->i_block[0] = first_block;
  inode->i_blocks   = vol->block_size / EXT2_SECTOR_SIZE;
  return 0;
}

/**
 * @brief Validate the path-target / split-name / resolve-parent triple.
 *
 * Same shape as @c ext2_create's parent validation but called out here
 * because mkdir also needs the "not already an entry" check (vs. create
 * which routes existing paths to open).
 *
 * @param vol           Volume.
 * @param path          Path to create.
 * @param out_parent_ino    Output parent inode number.
 * @param out_parent_inode  Output parent inode struct.
 * @param out_name      Output basename (≥ @c EXT2_NAME_MAX+1 bytes).
 * @return 0 on success, negative errno on validation failure.
 */
static i64 mkdir_validate_target(
    const ext2_volume_t *vol, const char *path, u32 *out_parent_ino,
    ext2_inode_t *out_parent_inode, char *out_name
)
{
  u32          existing_ino;
  ext2_inode_t existing_inode;
  if(resolve_path(vol, path, &existing_ino, &existing_inode) == 0)
    return -EEXIST;

  char parent_path[VFS_PATH_MAX];
  path_split(path, parent_path, out_name);
  if(out_name[0] == '\0')
    return -EINVAL;

  if(resolve_path(vol, parent_path, out_parent_ino, out_parent_inode) < 0)
    return -ENOENT;
  if((out_parent_inode->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;
  return 0;
}

/**
 * @brief Persist the new directory or roll back its allocations.
 *
 * Three steps must all succeed (seed-block status, write inode, link
 * into parent); any failure frees the seed block (if any) and the inode
 * so a half-created directory can't strand resources.
 *
 * @param vol           Volume.
 * @param new_ino       Newly-allocated inode number.
 * @param new_inode     Inode struct to persist.
 * @param parent_ino    Parent inode number.
 * @param parent_inode  Parent inode (mutated by @ref dir_add_entry).
 * @param dirname       Basename of the new directory.
 * @param seed_ret      Return code from @ref mkdir_seed_first_block.
 * @return 0 on success, propagated @p seed_ret on seed failure, -EIO
 *         on subsequent step failure.
 */
static i64 mkdir_finalize_or_rollback(
    ext2_volume_t *vol, u32 new_ino, ext2_inode_t *new_inode, u32 parent_ino,
    ext2_inode_t *parent_inode, const char *dirname, i64 seed_ret
)
{
  if(seed_ret == 0 && write_inode(vol, new_ino, new_inode) == 0 &&
     dir_add_entry(
         vol, parent_ino, parent_inode, dirname, new_ino, EXT2_FT_DIR
     ) == 0)
    return 0;
  if(new_inode->i_block[0])
    free_block(vol, new_inode->i_block[0]);
  free_inode(vol, new_ino, true);
  return seed_ret < 0 ? seed_ret : -EIO;
}

/**
 * @brief Create a directory on an ext2 volume.
 *
 * Linear bring-up with rollback delegated to @ref
 * mkdir_finalize_or_rollback.
 *
 * @param vol  Volume handle.
 * @param path Path for the new directory.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_mkdir(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  u32          parent_ino;
  ext2_inode_t parent_inode;
  char         dirname[EXT2_NAME_MAX + 1];
  i64          ret =
      mkdir_validate_target(vol, path, &parent_ino, &parent_inode, dirname);
  if(ret < 0)
    return ret;

  u32 preferred_grp = (parent_ino - 1) / vol->inodes_per_group;
  u32 new_ino       = alloc_inode(vol, preferred_grp, true);
  if(new_ino == 0)
    return -ENOSPC;

  ext2_inode_t new_inode;
  init_directory_inode(&new_inode, vol->block_size);
  i64 seed = mkdir_seed_first_block(
      vol, new_ino, &new_inode, parent_ino, preferred_grp
  );
  i64 fin = mkdir_finalize_or_rollback(
      vol, new_ino, &new_inode, parent_ino, &parent_inode, dirname, seed
  );
  if(fin < 0)
    return fin;

  parent_inode.i_links_count++;
  write_inode(vol, parent_ino, &parent_inode);
  flush_metadata(vol);
  return 0;
}

/**
 * @brief Remove a file from an ext2 volume.
 *
 * Decrements the link count; frees blocks/inode only when it reaches
 * zero — preserves hard-link semantics even though the rest of the
 * driver doesn't expose link/2.
 *
 * @param vol  Volume handle.
 * @param path Path to the file.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_unlink(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  u32          file_ino;
  ext2_inode_t file_inode;
  if(resolve_path(vol, path, &file_ino, &file_inode) < 0)
    return -ENOENT;
  if((file_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
    return -EISDIR;

  char parent_path[VFS_PATH_MAX];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;
  if(dir_remove_entry(vol, &parent_inode, filename) < 0)
    return -EIO;

  file_inode.i_links_count--;
  if(file_inode.i_links_count == 0) {
    bool is_open = false;
    for(int i = 0; i < EXT2_MAX_FILES; i++) {
      if(g_files[i].in_use && g_files[i].vol == vol &&
         g_files[i].inode_num == file_ino) {
        is_open = true;
        break;
      }
    }
    if(!is_open) {
      free_inode_blocks(vol, &file_inode);
      free_inode(vol, file_ino, false);
    } else {
      write_inode(vol, file_ino, &file_inode);
    }
  } else {
    write_inode(vol, file_ino, &file_inode);
  }
  flush_metadata(vol);
  return 0;
}

/**
 * @brief Remove an empty directory from an ext2 volume.
 *
 * Refuses non-empty directories (-ENOTEMPTY) and the root (-EINVAL) up
 * front so the destructive frees only run on cases that genuinely
 * collapse to "no longer reachable".
 *
 * @param vol  Volume handle.
 * @param path Path to the directory.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_rmdir(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;
  if(kstrcmp(path, "/") == 0)
    return -EINVAL;

  u32          dir_ino;
  ext2_inode_t dir_inode;
  if(resolve_path(vol, path, &dir_ino, &dir_inode) < 0)
    return -ENOENT;
  if((dir_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;
  if(!dir_is_empty(vol, &dir_inode))
    return -ENOTEMPTY;

  char parent_path[VFS_PATH_MAX];
  char dirname[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, dirname);

  if(kstrcmp(dirname, ".") == 0 || kstrcmp(dirname, "..") == 0)
    return -EINVAL;

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;
  if(dir_remove_entry(vol, &parent_inode, dirname) < 0)
    return -EIO;

  parent_inode.i_links_count--;
  write_inode(vol, parent_ino, &parent_inode);
  free_inode_blocks(vol, &dir_inode);
  free_inode(vol, dir_ino, true);
  flush_metadata(vol);
  return 0;
}
