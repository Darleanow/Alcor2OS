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
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

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
  char parent_path[VFS_PATH_MAX];
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
  char parent_path[VFS_PATH_MAX];
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
  char parent_path[VFS_PATH_MAX];
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
