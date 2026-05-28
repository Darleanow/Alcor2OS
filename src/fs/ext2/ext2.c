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

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <fs/ext2/internal.h>

/** @brief Open-file pool — defined here pending the file.c split. */
ext2_file_t g_files[EXT2_MAX_FILES];

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
  file->vol       = vol;
  file->inode_num = ino;
  file->inode     = inode;
  file->is_dir    = (inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
  file->in_use    = true;
  file->dirty     = false;

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
  char parent_path[VFS_PATH_MAX];
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
  file->vol       = vol;
  file->inode_num = new_ino;
  file->inode     = new_inode;
  file->is_dir    = false;
  file->in_use    = true;
  file->dirty     = false;

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
  file->dirty        = false; /* Inode is written below. */

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

/* --- VFS glue: fs_ops_t adapters (single surface; no duplicate ext2_vfs_*
 * layer)
 * --- */

static fs_handle_t ext2_ops_open(void *fs_data, const char *path, u32 flags)
{
  ext2_volume_t *v = fs_data;
  return (fs_handle_t)((flags & O_CREAT) ? ext2_create(v, path)
                                         : ext2_open(v, path));
}

static void ext2_ops_close(fs_handle_t fh)
{
  ext2_close((ext2_file_t *)fh);
}

static i64 ext2_ops_read(fs_handle_t fh, void *buf, u64 count, u64 offset)
{
  return ext2_read((ext2_file_t *)fh, buf, count, offset);
}

static i64
    ext2_ops_write(fs_handle_t fh, const void *buf, u64 count, u64 offset)
{
  return ext2_write((ext2_file_t *)fh, buf, count, offset);
}

static i64 ext2_ops_mkdir(void *fs_data, const char *path)
{
  return ext2_mkdir(fs_data, path);
}

static i64 ext2_ops_unlink(void *fs_data, const char *path)
{
  return ext2_unlink(fs_data, path);
}

static i64 ext2_ops_rmdir(void *fs_data, const char *path)
{
  return ext2_rmdir(fs_data, path);
}

static i64 ext2_ops_fstat(fs_handle_t fh, vfs_stat_t *st)
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

static i64 ext2_ops_stat(const void *fs_data, const char *path, vfs_stat_t *st)
{
  ext2_entry_t entry;
  i64          ret = ext2_stat(fs_data, path, &entry);
  if(ret == 0) {
    st->size = entry.size;
    st->type = (entry.file_type == EXT2_FT_DIR) ? VFS_DIRECTORY : VFS_FILE;
    st->ino  = entry.inode;
    st->dev  = 0;
  }
  return ret;
}

static i64
    ext2_ops_readdir(fs_handle_t fh, u64 index, char *name, vfs_stat_t *st)
{
  ext2_entry_t entry;
  i64          ret = ext2_readdir((ext2_file_t *)fh, index, &entry);
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

static i64 ext2_ops_truncate(fs_handle_t fh, u64 length)
{
  return ext2_truncate((ext2_file_t *)fh, length);
}

static i64
    ext2_ops_readlink(const void *fs_data, const char *path, char *buf, u64 cap)
{
  return ext2_readlink(fs_data, path, buf, cap);
}

static void *ext2_ops_mount(const char *source, u32 flags)
{
  (void)source;
  (void)flags;
  return ext2_mount(g_default_dev, 0);
}

static const fs_ops_t g_ext2_fs_ops = {
    .open     = ext2_ops_open,
    .close    = ext2_ops_close,
    .read     = ext2_ops_read,
    .write    = ext2_ops_write,
    .mkdir    = ext2_ops_mkdir,
    .unlink   = ext2_ops_unlink,
    .rmdir    = ext2_ops_rmdir,
    .stat     = ext2_ops_stat,
    .fstat    = ext2_ops_fstat,
    .readdir  = ext2_ops_readdir,
    .truncate = ext2_ops_truncate,
    .readlink = ext2_ops_readlink,
};

const fs_type_t g_ext2_fstype = {
    .name  = "ext2",
    .ops   = &g_ext2_fs_ops,
    .mount = ext2_ops_mount,
};
