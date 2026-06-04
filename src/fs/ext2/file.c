/**
 * @file src/fs/ext2/file.c
 * @brief File open / close / read / write / truncate / flush — the public
 *        ext2 byte-oriented API.
 *
 * Owns the open-file pool (@ref g_files) and bridges between the public
 * @c ext2_* API and the block-mapping / inode helpers in the rest of the
 * module. The read / write loops delegate one-iteration chunks to small
 * helpers so the public functions read as a control flow instead of as
 * three nested concerns at once.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/** @brief Open-file pool. Sized at @ref EXT2_MAX_FILES to cover a typical
 * shell session plus background daemons without ever evicting a handle. */
ext2_file_t g_files[EXT2_MAX_FILES];

/**
 * @brief Claim the first free slot in @ref g_files.
 *
 * Linear scan is fine at @ref EXT2_MAX_FILES: the pool is small enough
 * that a freelist would be overhead without measurable benefit.
 *
 * @return Slot pointer, or NULL when the pool is exhausted.
 */
static ext2_file_t *claim_free_file_slot(void)
{
  for(int i = 0; i < EXT2_MAX_FILES; i++) {
    if(!g_files[i].in_use)
      return &g_files[i];
  }
  return NULL;
}

/**
 * @brief Populate a file handle from a resolved inode.
 *
 * Both @ref ext2_open and @ref ext2_create end on the same field-fill
 * pattern; centralising it keeps the "what does an opened handle look
 * like" definition in one place.
 *
 * @param file   Slot to fill (already claimed via @ref claim_free_file_slot).
 * @param vol    Owning volume.
 * @param ino    Inode number.
 * @param inode  Inode data to copy into the handle.
 */
static void fill_file_handle(
    ext2_file_t *file, ext2_volume_t *vol, u32 ino, const ext2_inode_t *inode
)
{
  file->vol       = vol;
  file->inode_num = ino;
  file->inode     = *inode;
  file->is_dir    = (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
  file->in_use    = true;
  file->dirty     = false;
}

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
  ext2_file_t *file = claim_free_file_slot();
  if(!file)
    return NULL;
  u32          ino;
  ext2_inode_t inode;
  if(resolve_path(vol, path, &ino, &inode) < 0)
    return NULL;
  fill_file_handle(file, vol, ino, &inode);
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

  if(file->inode.i_links_count == 0) {
    int open_count = 0;
    for(int i = 0; i < EXT2_MAX_FILES; i++) {
      if(g_files[i].in_use && g_files[i].vol == file->vol &&
         g_files[i].inode_num == file->inode_num) {
        open_count++;
      }
    }
    if(open_count == 1) {
      free_inode_blocks(file->vol, &file->inode);
      free_inode(file->vol, file->inode_num, false);
      flush_metadata(file->vol);
      file->in_use = false;
      return;
    }
  }

  if(file->dirty) {
    write_inode(file->vol, file->inode_num, &file->inode);
    flush_metadata(file->vol);
  }

  file->in_use = false;
}

/**
 * @brief Fill @p dst with zeros for a sparse-file hole.
 *
 * Sparse blocks aren't on disk, but POSIX read must still return zeros
 * for them. Centralised so the read loop is symmetric with the on-disk
 * paths.
 *
 * @param dst           Destination buffer.
 * @param block_offset  Starting byte inside the missing block.
 * @param block_size    Volume block size.
 * @param remaining     Bytes still wanted by the caller.
 * @return Bytes filled (capped at the block remainder).
 */
static u64
    read_sparse_chunk(u8 *dst, u32 block_offset, u32 block_size, u64 remaining)
{
  u64 to_read = block_size - block_offset;
  if(to_read > remaining)
    to_read = remaining;
  kzero(dst, to_read);
  return to_read;
}

/**
 * @brief Count how many on-disk blocks following @p base_block_num are
 *        contiguous to it for this inode.
 *
 * Bounded by @p max_run because run reads consume one large buffer and
 * we don't want to balloon the read past what the caller still needs.
 *
 * @param vol             Volume.
 * @param inode           Inode owning the file.
 * @param file_block      File-relative index of the first block in the run.
 * @param base_block_num  On-disk block number of the first block in the run.
 * @param max_run         Cap on the returned run length.
 * @return Number of contiguous blocks (always ≥ 1).
 */
static u32 detect_run_length(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block,
    u32 base_block_num, u32 max_run
)
{
  u32 run = 1;
  while(run < max_run) {
    u32 nxt = get_block_num(vol, inode, file_block + run);
    if(nxt != base_block_num + run)
      break;
    run++;
  }
  return run;
}

/**
 * @brief Read one block, copy a slice of it into @p dst.
 *
 * Single-block path used both when no run buffer is available and when
 * the run detector returns 1. Uses the small @p block_buf cache so we
 * don't kmalloc per iteration.
 *
 * @param vol           Volume.
 * @param block_num     Block to read.
 * @param block_buf     Scratch buffer of @c vol->block_size bytes.
 * @param block_offset  Starting byte inside the block.
 * @param dst           Destination.
 * @param remaining     Bytes still wanted by the caller.
 * @param block_size    Volume block size.
 * @return Bytes copied, or -EIO on read failure.
 */
static i64 read_single_block_chunk(
    const ext2_volume_t *vol, u32 block_num, u8 *block_buf, u32 block_offset,
    u8 *dst, u64 remaining, u32 block_size
)
{
  if(vol_read_block(vol, block_num, block_buf) < 0)
    return -EIO;
  u64 to_read = block_size - block_offset;
  if(to_read > remaining)
    to_read = remaining;
  kmemcpy(dst, block_buf + block_offset, to_read);
  return (i64)to_read;
}

/**
 * @brief Read @p run consecutive blocks in one device call, copy slice
 *        into @p dst.
 *
 * Saves N-1 device round trips when blocks are contiguous on disk. Falls
 * back automatically: if the run detector returns 1, the caller picks
 * @ref read_single_block_chunk instead.
 *
 * @param vol           Volume.
 * @param base_block    First block in the run.
 * @param run           Run length (≥ 2 in practice).
 * @param run_buf       Caller-provided buffer ≥ @c run @c * @c block_size.
 * @param block_offset  Starting byte inside @p base_block.
 * @param dst           Destination.
 * @param remaining     Bytes still wanted by the caller.
 * @param block_size    Volume block size.
 * @return Bytes copied, or -EIO on read failure.
 */
static i64 read_run_chunk(
    const ext2_volume_t *vol, u32 base_block, u32 run, u8 *run_buf,
    u32 block_offset, u8 *dst, u64 remaining, u32 block_size
)
{
  u32 sectors  = run * (block_size / EXT2_SECTOR_SIZE);
  u32 base_sec = base_block * (block_size / EXT2_SECTOR_SIZE);
  if(vol_read_sectors(vol, base_sec, sectors, run_buf) < 0)
    return -EIO;
  u64 avail   = (u64)run * block_size - block_offset;
  u64 to_read = remaining < avail ? remaining : avail;
  kmemcpy(dst, run_buf + block_offset, to_read);
  return (i64)to_read;
}

/**
 * @brief One iteration of the read loop: pick the right path for one
 *        block and copy as much as fits.
 *
 * Three-way dispatch — sparse hole / multi-block run / single block —
 * pulled out so @ref ext2_read reads as a "loop until done, handle
 * errors" wrapper instead of carrying the dispatch inside the loop.
 *
 * @param vol           Volume.
 * @param inode         Inode owning the file.
 * @param current_pos   Byte offset inside the file.
 * @param remaining     Bytes still wanted by the caller.
 * @param dst           Destination for this iteration only.
 * @param block_buf     Scratch single-block buffer.
 * @param run_buf       Optional multi-block buffer (NULL falls back).
 * @param block_size    Volume block size.
 * @return Bytes copied, or negative errno on failure.
 */
static i64 read_one_iteration(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u64 current_pos,
    u64 remaining, u8 *dst, u8 *block_buf, u8 *run_buf, u32 block_size
)
{
  u32 file_block   = current_pos / block_size;
  u32 block_offset = current_pos % block_size;
  u32 block_num    = get_block_num(vol, inode, file_block);
  if(block_num == 0)
    return (i64)read_sparse_chunk(dst, block_offset, block_size, remaining);
  if(run_buf) {
    u32 max_run =
        (u32)((remaining + block_offset + block_size - 1) / block_size);
    if(max_run > EXT2_READ_RUN_MAX)
      max_run = EXT2_READ_RUN_MAX;
    u32 run = detect_run_length(vol, inode, file_block, block_num, max_run);
    if(run > 1)
      return read_run_chunk(
          vol, block_num, run, run_buf, block_offset, dst, remaining, block_size
      );
  }
  return read_single_block_chunk(
      vol, block_num, block_buf, block_offset, dst, remaining, block_size
  );
}

/**
 * @brief Drive the read loop with pre-allocated buffers.
 *
 * Pulled out so @ref ext2_read can own buffer lifecycle without inlining
 * a 15-line loop. Partial-read on chunk error matches POSIX: surface the
 * data the caller already received instead of discarding it.
 *
 * @param file        Open file handle.
 * @param buf         Destination buffer.
 * @param count       Bytes to read (already clipped to EOF by caller).
 * @param offset      Byte offset within the file.
 * @param block_buf   Scratch single-block buffer.
 * @param run_buf     Optional run buffer (NULL skips the fast path).
 * @return Bytes read, or negative errno when nothing was read.
 */
static i64 read_with_buffers(
    ext2_file_t *file, void *buf, u64 count, u64 offset, u8 *block_buf,
    u8 *run_buf
)
{
  u8 *dst        = (u8 *)buf;
  u64 bytes_read = 0;
  while(bytes_read < count) {
    i64 chunk = read_one_iteration(
        file->vol, &file->inode, offset + bytes_read, count - bytes_read,
        dst + bytes_read, block_buf, run_buf, file->vol->block_size
    );
    if(chunk < 0)
      return bytes_read > 0 ? (i64)bytes_read : -EIO;
    bytes_read += (u64)chunk;
  }
  return (i64)bytes_read;
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
  if(offset >= file->inode.i_size)
    return 0;
  if(offset + count > file->inode.i_size)
    count = file->inode.i_size - offset;

  u32 block_size = file->vol->block_size;
  u8 *block_buf  = cache_get_block(block_size);
  if(!block_buf)
    return -ENOMEM;
  u8 *run_buf = kmalloc((u64)EXT2_READ_RUN_MAX * block_size);

  i64 ret = read_with_buffers(file, buf, count, offset, block_buf, run_buf);

  if(run_buf)
    kfree(run_buf);
  cache_put_block(block_buf);
  return ret;
}

/**
 * @brief Resolve @p file_block to an on-disk block, allocating on hole.
 *
 * Pulled out of @ref write_one_chunk so the write-path "ensure the slot
 * exists" step reads as one call; sets @c file->dirty so the size update
 * that follows a fresh alloc travels back to disk.
 *
 * @param file           File being written (mutated: dirty flag).
 * @param file_block     File-relative block index.
 * @param preferred_grp  Locality hint for new blocks.
 * @return Block number, or 0 if @ref alloc_file_block fails.
 */
static u32
    ensure_data_block(ext2_file_t *file, u32 file_block, u32 preferred_grp)
{
  u32 block_num = get_block_num(file->vol, &file->inode, file_block);
  if(block_num == 0) {
    block_num =
        alloc_file_block(file->vol, &file->inode, file_block, preferred_grp);
    if(block_num == 0)
      return 0;
    file->dirty = true;
  }
  return block_num;
}

/**
 * @brief One iteration of the write loop: place one chunk into the file.
 *
 * Read-modify-write only when the chunk doesn't cover the whole block
 * (offset != 0 or short tail); whole-block writes skip the read. Bumps
 * @c i_size + dirty flag when the write extends EOF.
 *
 * @param file           Open file handle (mutated: i_size, dirty).
 * @param current_pos    Byte offset inside the file.
 * @param src            Source for this iteration only.
 * @param remaining      Bytes still pending in the caller's write.
 * @param block_buf      Scratch single-block buffer.
 * @param preferred_grp  Locality hint for new blocks.
 * @return Bytes written, or negative errno on failure.
 */
static i64 write_one_chunk(
    ext2_file_t *file, u64 current_pos, const u8 *src, u64 remaining,
    u8 *block_buf, u32 preferred_grp
)
{
  ext2_volume_t *vol          = file->vol;
  u32            block_size   = vol->block_size;
  u32            file_block   = current_pos / block_size;
  u32            block_offset = current_pos % block_size;

  u32            block_num = ensure_data_block(file, file_block, preferred_grp);
  if(block_num == 0)
    return -ENOSPC;

  u64 to_write = block_size - block_offset;
  if(to_write > remaining)
    to_write = remaining;
  if(block_offset != 0 || to_write < block_size) {
    if(vol_read_block(vol, block_num, block_buf) < 0)
      return -EIO;
  }
  kmemcpy(block_buf + block_offset, src, to_write);
  if(vol_write_block(vol, block_num, block_buf) < 0)
    return -EIO;

  if(current_pos + to_write > file->inode.i_size) {
    file->inode.i_size = current_pos + to_write;
    file->dirty        = true;
  }
  return (i64)to_write;
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
/**
 * @brief Drive the write loop with a pre-allocated block buffer.
 *
 * Companion to @ref read_with_buffers; same partial-write-on-error rule.
 *
 * @param file           File being written.
 * @param buf            Source.
 * @param count          Bytes to write.
 * @param offset         Byte offset within the file.
 * @param block_buf      Scratch single-block buffer.
 * @param preferred_grp  Locality hint for new blocks.
 * @return Bytes written, or negative errno when nothing was written.
 */
static i64 write_with_buffer(
    ext2_file_t *file, const void *buf, u64 count, u64 offset, u8 *block_buf,
    u32 preferred_grp
)
{
  const u8 *src           = (const u8 *)buf;
  u64       bytes_written = 0;
  while(bytes_written < count) {
    i64 chunk = write_one_chunk(
        file, offset + bytes_written, src + bytes_written,
        count - bytes_written, block_buf, preferred_grp
    );
    if(chunk < 0)
      return bytes_written > 0 ? (i64)bytes_written : chunk;
    bytes_written += (u64)chunk;
  }
  return (i64)bytes_written;
}

i64 ext2_write(ext2_file_t *file, const void *buf, u64 count, u64 offset)
{
  if(!file || !file->in_use || file->is_dir)
    return -EINVAL;
  if(count == 0)
    return 0;

  ext2_volume_t *vol           = file->vol;
  u32            preferred_grp = (file->inode_num - 1) / vol->inodes_per_group;
  u8            *block_buf     = cache_get_block(vol->block_size);
  if(!block_buf)
    return -ENOMEM;

  i64 ret =
      write_with_buffer(file, buf, count, offset, block_buf, preferred_grp);

  cache_put_block(block_buf);
  if(file->dirty)
    write_inode(vol, file->inode_num, &file->inode);
  return ret;
}

/**
 * @brief Allocate a fresh inode, link it into @p parent_inode under
 *        @p filename, and persist the metadata.
 *
 * Wraps the alloc / write-inode / dir-add / flush sequence so
 * @ref ext2_create stays a control-flow wrapper. Rolls back the inode
 * alloc on any downstream failure so a half-created file doesn't leak.
 *
 * @param vol           Volume.
 * @param parent_ino    Parent directory inode number.
 * @param parent_inode  Parent directory inode (mutated by @ref dir_add_entry).
 * @param filename      Name of the new entry under @p parent_inode.
 * @param out_inode     Output buffer for the new inode contents.
 * @return New inode number, or 0 on failure.
 */
static u32 create_file_inode_and_link(
    ext2_volume_t *vol, u32 parent_ino, ext2_inode_t *parent_inode,
    const char *filename, ext2_inode_t *out_inode
)
{
  u32 preferred_grp = (parent_ino - 1) / vol->inodes_per_group;
  u32 new_ino       = alloc_inode(vol, preferred_grp, false);
  if(new_ino == 0)
    return 0;

  kzero(out_inode, sizeof(ext2_inode_t));
  out_inode->i_mode        = EXT2_S_IFREG | 0644;
  out_inode->i_links_count = 1;

  if(write_inode(vol, new_ino, out_inode) < 0 ||
     dir_add_entry(
         vol, parent_ino, parent_inode, filename, new_ino, EXT2_FT_REG_FILE
     ) < 0) {
    free_inode(vol, new_ino, false);
    return 0;
  }
  flush_metadata(vol);
  return new_ino;
}

/**
 * @brief Resolve and validate the parent directory for a creation op.
 *
 * Two pieces are joined here so the caller doesn't repeat the "resolve
 * then check S_IFDIR" pattern: if either step fails, the parent isn't
 * usable for creating a child entry.
 *
 * @param vol         Volume.
 * @param parent_path Absolute path of the directory.
 * @param out_ino     Output inode number.
 * @param out_inode   Output inode struct.
 * @return @c true if the parent exists and is a directory.
 */
static bool validate_parent_for_create(
    const ext2_volume_t *vol, const char *parent_path, u32 *out_ino,
    ext2_inode_t *out_inode
)
{
  if(resolve_path(vol, parent_path, out_ino, out_inode) < 0)
    return false;
  return (out_inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * @brief Create a new file on an ext2 volume.
 *
 * If the file already exists, opens it instead — matches POSIX
 * @c O_CREAT semantics without @c O_EXCL.
 *
 * @param vol  Volume handle.
 * @param path Path for the new file.
 * @return File handle, or NULL on failure.
 */
ext2_file_t *ext2_create(ext2_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return NULL;
  u32          existing_ino;
  ext2_inode_t existing_inode;
  if(resolve_path(vol, path, &existing_ino, &existing_inode) == 0)
    return ext2_open(vol, path);

  ext2_file_t *file = claim_free_file_slot();
  if(!file)
    return NULL;

  char parent_path[VFS_PATH_MAX];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);
  if(filename[0] == '\0')
    return NULL;

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(!validate_parent_for_create(vol, parent_path, &parent_ino, &parent_inode))
    return NULL;

  ext2_inode_t new_inode;
  u32          new_ino = create_file_inode_and_link(
      vol, parent_ino, &parent_inode, filename, &new_inode
  );
  if(new_ino == 0)
    return NULL;

  fill_file_handle(file, vol, new_ino, &new_inode);
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
