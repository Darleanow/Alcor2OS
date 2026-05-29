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
