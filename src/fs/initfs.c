/**
 * @file src/fs/initfs.c
 * @brief Read-only, flat ramfs populated from Limine boot modules.
 *
 * See @c include/alcor2/fs/initfs.h. The kernel walks the boot-module list
 * during early init, registers each @c /boot/bin/ ELF via
 * ::initfs_register, then mounts the result on @c /init so the core
 * utilities ride along inside the ISO; the persistent ext2 disk holds
 * everything else.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/initfs.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>

/** @brief Magic @c st_dev value reported by every initfs stat result. */
#define INITFS_ST_DEV 0x696E69746673ULL /* "initfs" */

/** @brief Sentinel handle returned by ::init_open for the root directory.
 *
 * Distinct from any heap pointer and from @c NULL so read/stat handlers can
 * distinguish "this is the root" from "this is a file node" with a pointer
 * compare. */
#define INITFS_ROOT_HANDLE ((fs_handle_t)(uintptr_t)0x1)

/** @brief Single virtual file entry; one node per registered binary. */
typedef struct init_file
{
  char              name[VFS_NAME_MAX]; /**< Leaf filename (no slashes). */
  const u8         *data;               /**< Borrowed pointer; not freed. */
  u64               size;
  struct init_file *next;
} init_file_t;

/** @brief Head of the registered-file list. */
static init_file_t *files = NULL;

/**
 * @brief Strip the leading slash from a VFS-relative path.
 *
 * Paths from the VFS are always absolute (start with @c /). Returns the
 * empty string for the root, which the open/stat paths treat specially.
 *
 * @param path  VFS-relative path, may be @c NULL.
 * @return Pointer past the leading slash, the original pointer when no
 *         slash is present, or @c NULL when @p path is @c NULL.
 */
static const char *strip_slash(const char *path)
{
  if(!path)
    return NULL;
  if(path[0] == '/')
    return path + 1;
  return path;
}

/**
 * @brief Linear lookup in the registered-file list.
 *
 * @param name  Filename to match (case-sensitive, no slashes).
 * @return Matching node or @c NULL if no entry has that name.
 */
static init_file_t *lookup(const char *name)
{
  for(init_file_t *f = files; f; f = f->next) {
    if(kstreq(f->name, name))
      return f;
  }
  return NULL;
}

/**
 * @brief Open the root directory or a registered file.
 *
 * Initfs is read-only; any write-side flag fails the open. The namespace is
 * flat (no nested paths).
 *
 * @param fs_data  Ignored — initfs holds a single global table.
 * @param path     Mount-relative path; "/" returns the root sentinel.
 * @param flags    POSIX open flags; only read-side combinations are allowed.
 * @return Root sentinel for "/", a node pointer for a registered file, or
 *         @c NULL on error / write-side flags / missing file.
 */
static fs_handle_t init_open(void *fs_data, const char *path, u32 flags)
{
  (void)fs_data;
  if(flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))
    return NULL;

  const char *name = strip_slash(path);
  if(!name || name[0] == '\0')
    return INITFS_ROOT_HANDLE;

  for(const char *p = name; *p; p++) {
    if(*p == '/')
      return NULL;
  }

  init_file_t *f = lookup(name);
  return f ? (fs_handle_t)f : NULL;
}

/** @brief Nothing to release — handles are sentinels or kernel-lifetime
 * node pointers. Kept non-NULL because not every VFS close path is
 * NULL-guarded. */
static void init_close(fs_handle_t fh)
{
  (void)fh;
}

/**
 * @brief Copy bytes from a registered file into a caller buffer.
 *
 * @param fh      Open file handle (root sentinel returns @c -EISDIR).
 * @param buf     Destination buffer.
 * @param count   Maximum bytes to copy.
 * @param offset  Byte offset within the file.
 * @return Bytes copied (0 at EOF), or negative @c -errno on error.
 */
static i64 init_read(fs_handle_t fh, void *buf, u64 count, u64 offset)
{
  if(fh == INITFS_ROOT_HANDLE)
    return -EISDIR;
  const init_file_t *f = (const init_file_t *)fh;
  if(offset >= f->size)
    return 0;
  u64 avail = f->size - offset;
  if(count > avail)
    count = avail;
  kmemcpy(buf, f->data + offset, count);
  return (i64)count;
}

/**
 * @brief Always fails with @c -EROFS; initfs is read-only.
 *
 * @param fh      Ignored.
 * @param buf     Ignored.
 * @param count   Ignored.
 * @param offset  Ignored.
 * @return Always @c -EROFS.
 */
static i64 init_write(fs_handle_t fh, const void *buf, u64 count, u64 offset)
{
  (void)fh;
  (void)buf;
  (void)count;
  (void)offset;
  return -EROFS;
}

/**
 * @brief Populate a stat buffer from a file node, or fill the directory
 * stub when no node is given.
 *
 * @param st  Destination stat buffer.
 * @param f   File node, or @c NULL to describe the root directory.
 */
static void fill_stat(vfs_stat_t *st, const init_file_t *f)
{
  if(f) {
    st->size = f->size;
    st->type = VFS_FILE;
    st->ino  = (u64)(uintptr_t)f;
  } else {
    st->size = 0;
    st->type = VFS_DIRECTORY;
    st->ino  = 0;
  }
  st->dev      = INITFS_ST_DEV;
  st->created  = 0;
  st->modified = 0;
}

/**
 * @brief Stat a path. The empty/root path resolves to the directory stub.
 *
 * @param fs_data  Ignored.
 * @param path     Mount-relative path.
 * @param st       Destination stat buffer.
 * @return 0 on success, @c -ENOENT if the path does not exist.
 */
static i64 init_stat(void *fs_data, const char *path, vfs_stat_t *st)
{
  (void)fs_data;
  const char *name = strip_slash(path);
  if(!name || name[0] == '\0') {
    fill_stat(st, NULL);
    return 0;
  }
  const init_file_t *f = lookup(name);
  if(!f)
    return -ENOENT;
  fill_stat(st, f);
  return 0;
}

/**
 * @brief Stat an open handle (root sentinel or file node).
 *
 * @param fh  Handle returned by ::init_open.
 * @param st  Destination stat buffer.
 * @return Always 0.
 */
static i64 init_fstat(fs_handle_t fh, vfs_stat_t *st)
{
  if(fh == INITFS_ROOT_HANDLE) {
    fill_stat(st, NULL);
    return 0;
  }
  fill_stat(st, (const init_file_t *)fh);
  return 0;
}

/**
 * @brief Yield the @p index'th entry in the root directory.
 *
 * @param fh     Must be the root sentinel; non-root handles get @c -ENOTDIR.
 * @param index  Zero-based entry index.
 * @param name   Caller buffer of at least ::VFS_NAME_MAX + 1 bytes.
 * @param st     Optional stat buffer for the entry; may be @c NULL.
 * @return 1 when an entry is yielded, 0 at end-of-directory, negative @c
 *         -errno otherwise.
 */
static i64 init_readdir(fs_handle_t fh, u64 index, char *name, vfs_stat_t *st)
{
  if(fh != INITFS_ROOT_HANDLE)
    return -ENOTDIR;

  init_file_t *f = files;
  for(u64 i = 0; i < index && f; i++)
    f = f->next;
  if(!f)
    return 0;

  kstrncpy(name, f->name, VFS_NAME_MAX);
  if(st)
    fill_stat(st, f);
  return 1;
}

/** @brief Operations table — only read-side and stat are wired. */
static const fs_ops_t init_ops = {
    .open    = init_open,
    .close   = init_close,
    .read    = init_read,
    .write   = init_write,
    .stat    = init_stat,
    .fstat   = init_fstat,
    .readdir = init_readdir,
};

/**
 * @brief Mount callback. Single global table; non-NULL signals success.
 *
 * @param source  Ignored.
 * @param flags   Ignored.
 * @return Sentinel non-NULL pointer.
 */
static void *init_mount_cb(const char *source, u32 flags)
{
  (void)source;
  (void)flags;
  return (void *)1;
}

/** @brief Filesystem type descriptor registered with the VFS. */
static const fs_type_t init_fstype = {
    .name  = "initfs",
    .ops   = &init_ops,
    .mount = init_mount_cb,
};

void initfs_init(void)
{
  /* Idempotent: safe to call from both early init and a later boot phase. */
  static bool registered = false;
  if(registered)
    return;
  vfs_register_fs(&init_fstype);
  registered = true;
}

i64 initfs_register(const char *name, const void *data, u64 size)
{
  if(!name || !data || name[0] == '\0')
    return -EINVAL;
  for(const char *p = name; *p; p++) {
    if(*p == '/')
      return -EINVAL;
  }
  if(lookup(name))
    return -EEXIST;

  init_file_t *f = kzalloc(sizeof(*f));
  if(!f)
    return -ENOMEM;
  kstrncpy(f->name, name, VFS_NAME_MAX);
  f->data = (const u8 *)data;
  f->size = size;
  f->next = files;
  files   = f;
  return 0;
}
