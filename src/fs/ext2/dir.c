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
