/**
 * @file src/fs/ext2/ops.c
 * @brief @c fs_ops_t bridge for ext2.
 *
 * Thin shims that translate VFS-shaped calls (handles + paths) into the
 * @c ext2_* public API. Owns the @ref g_ext2_fstype registration record that
 * @c super.c's @ref ext2_init publishes through @c vfs_register_fs.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief @c fs_ops_t.open shim — routes O_CREAT to @ref ext2_create, else
 * to @ref ext2_open.
 *
 * Keeping the create/open choice in the bridge rather than inside the ext2
 * core preserves the core's "open never creates" invariant.
 *
 * @param fs_data  Mounted @c ext2_volume_t* (typed through @c void* by VFS).
 * @param path     Absolute path inside the mount.
 * @param flags    VFS open flags.
 * @return Opaque handle to an ext2_file_t, or NULL on failure.
 */
static fs_handle_t ext2_ops_open(void *fs_data, const char *path, u32 flags)
{
  ext2_volume_t *v = fs_data;
  return (fs_handle_t)((flags & O_CREAT) ? ext2_create(v, path)
                                         : ext2_open(v, path));
}

/**
 * @brief @c fs_ops_t.close shim.
 * @param fh  Handle from @ref ext2_ops_open.
 */
static void ext2_ops_close(fs_handle_t fh)
{
  ext2_close((ext2_file_t *)fh);
}

/**
 * @brief @c fs_ops_t.read shim.
 * @param fh      Open file handle.
 * @param buf     Destination buffer.
 * @param count   Bytes to read.
 * @param offset  Byte offset within the file.
 * @return Bytes read, 0 at EOF, negative errno on error.
 */
static i64 ext2_ops_read(fs_handle_t fh, void *buf, u64 count, u64 offset)
{
  return ext2_read((ext2_file_t *)fh, buf, count, offset);
}

/**
 * @brief @c fs_ops_t.write shim.
 * @param fh      Open file handle.
 * @param buf     Source buffer.
 * @param count   Bytes to write.
 * @param offset  Byte offset within the file.
 * @return Bytes written or negative errno.
 */
static i64
    ext2_ops_write(fs_handle_t fh, const void *buf, u64 count, u64 offset)
{
  return ext2_write((ext2_file_t *)fh, buf, count, offset);
}

/**
 * @brief @c fs_ops_t.mkdir shim.
 * @param fs_data  Mounted volume.
 * @param path     Absolute path for the new directory.
 * @return 0 on success or negative errno.
 */
static i64 ext2_ops_mkdir(void *fs_data, const char *path)
{
  return ext2_mkdir(fs_data, path);
}

/**
 * @brief @c fs_ops_t.unlink shim.
 * @param fs_data  Mounted volume.
 * @param path     Absolute path of the file to remove.
 * @return 0 on success or negative errno.
 */
static i64 ext2_ops_unlink(void *fs_data, const char *path)
{
  return ext2_unlink(fs_data, path);
}

/**
 * @brief @c fs_ops_t.rmdir shim.
 * @param fs_data  Mounted volume.
 * @param path     Absolute path of the (empty) directory to remove.
 * @return 0 on success or negative errno.
 */
static i64 ext2_ops_rmdir(void *fs_data, const char *path)
{
  return ext2_rmdir(fs_data, path);
}

/**
 * @brief @c fs_ops_t.fstat shim — fills @p st from an open file's inode.
 *
 * The in-memory inode is authoritative because the open handle's copy is
 * fresher than a re-read would be (writes hit the handle first, flush
 * later); reading off the handle keeps stat consistent with the live file.
 *
 * @param fh  Open file handle.
 * @param st  Output VFS stat.
 * @return 0 on success or -EINVAL when @p fh / @p st is unusable.
 */
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

/**
 * @brief @c fs_ops_t.stat shim — path-based stat.
 *
 * Mirrors @ref ext2_ops_fstat for the no-open-handle case. The
 * @c st->dev = 0 line is deliberate: ext2 doesn't expose a device-id, and
 * leaving it uninitialised would let callers fingerprint stack garbage.
 *
 * @param fs_data  Mounted volume.
 * @param path     Absolute path.
 * @param st       Output VFS stat.
 * @return 0 on success or negative errno.
 */
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

/**
 * @brief @c fs_ops_t.readdir shim — copies one entry's name + stat.
 *
 * VFS wants name and stat split; ext2_readdir returns them packed in one
 * @c ext2_entry_t, so the shim unpacks. @p st is optional so callers that
 * only need names (e.g. shell tab completion) can pass NULL and skip the
 * file-type / size assignment.
 *
 * @param fh     Open directory handle.
 * @param index  Zero-based entry index.
 * @param name   Output name buffer (≥ VFS_NAME_MAX).
 * @param st     Optional output VFS stat; NULL skips stat fill.
 * @return 1 on entry, 0 at end, negative errno on error.
 */
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

/**
 * @brief @c fs_ops_t.truncate shim.
 * @param fh      Open file handle.
 * @param length  New file length in bytes.
 * @return 0 on success or negative errno.
 */
static i64 ext2_ops_truncate(fs_handle_t fh, u64 length)
{
  return ext2_truncate((ext2_file_t *)fh, length);
}

/**
 * @brief @c fs_ops_t.readlink shim.
 * @param fs_data  Mounted volume.
 * @param path     Absolute path of the symlink.
 * @param buf      Output buffer.
 * @param cap      Buffer capacity.
 * @return Bytes written (excluding NUL) or negative errno.
 */
static i64
    ext2_ops_readlink(const void *fs_data, const char *path, char *buf, u64 cap)
{
  return ext2_readlink(fs_data, path, buf, cap);
}

/**
 * @brief @c fs_type_t.mount shim — opens the default device.
 *
 * @p source and @p flags are unused by this driver because the device is
 * already chosen at @ref ext2_init time and ext2 has no per-mount options
 * we honour yet; both are swallowed with @c (void) to avoid -Wunused.
 *
 * @param source  Ignored.
 * @param flags   Ignored.
 * @return Opaque @c ext2_volume_t* on success, NULL on failure.
 */
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
