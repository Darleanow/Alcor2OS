/**
 * @file src/fs/ext2/file.c
 * @brief File open / close / read / write / truncate / flush — the public
 *        ext2 byte-oriented API.
 *
 * Owns the open-file pool (@ref g_files) and bridges between the public
 * @c ext2_* API and the block-mapping / inode helpers in the rest of the
 * module.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/** @brief Open-file pool. Sized at @ref EXT2_MAX_FILES to cover a typical
 * shell session plus background daemons without ever evicting a handle. */
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
 * @brief Read up to @p count bytes from @p file at byte @p offset.
 *
 * Pread-style: takes @p offset directly instead of mutating an
 * internal file position, so the VFS layer can keep per-fd state and
 * concurrent reads on the same handle don't trample each other.
 *
 * @param file    Open file handle.
 * @param buf     Destination buffer.
 * @param count   Maximum bytes to read.
 * @param offset  Byte offset within the file.
 * @return Bytes read, 0 at EOF, or negative errno on error.
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
 * @brief Write @p count bytes to @p file at byte @p offset.
 *
 * Pwrite-style (see @ref ext2_read for the rationale). Allocates new
 * blocks lazily via @ref alloc_file_block as the write crosses block
 * boundaries; writes past EOF extend the file and update @c i_size.
 *
 * @param file    Open file handle.
 * @param buf     Source buffer.
 * @param count   Bytes to write.
 * @param offset  Byte offset within the file.
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
