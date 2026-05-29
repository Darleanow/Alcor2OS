/**
 * @file src/fs/ext2/dir.c
 * @brief Directory entry helpers — lookup, insert, remove, emptiness check.
 *
 * The on-disk directory format is a sequence of variable-length records
 * inside the directory's data blocks; every helper here walks that
 * structure with the same offset / @c rec_len arithmetic. @c file.c's
 * higher-level @c ext2_readdir / @c ext2_mkdir / @c ext2_unlink / @c ext2_rmdir
 * are the public consumers.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Find a directory entry by name.
 * @param vol Volume.
 * @param dir_inode Directory inode.
 * @param name Entry name to find.
 * @param out_ino Output inode number if found.
 * @param out_type Output file type if found.
 * @return 0 on success, -ENOENT if not found.
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
i64 dir_add_entry(
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
bool dir_is_empty(const ext2_volume_t *vol, const ext2_inode_t *dir_inode)
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
