/**
 * @file src/fs/vfs.c
 * @brief Virtual File System orchestration layer.
 *
 * Implements mount management, the Open File Table (OFT), per-process fd
 * tables, path normalisation, and dispatch to registered filesystem drivers.
 *
 * @par Mount resolution
 * Paths are made absolute with ::vfs_make_absolute — relative paths are
 * anchored to the calling process's CWD stored in @c proc_t::cwd — then
 * normalised in-place by ::vfs_normalize.  ::vfs_find_mount performs
 * longest-prefix matching over the active mount table to select the
 * responsible driver.
 *
 * @par Open File Table lifetime
 * Each ::vfs_oft_entry_t carries a reference count.  ::vfs_oft_retain
 * increments it (fork, dup) and ::vfs_oft_release decrements it; when the
 * count reaches zero the driver's @c close or ::pipe_oft_release is invoked
 * and the slot is returned to the pool.  Per-process fd numbers are small
 * integers that index into this shared table.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

#define VFS_MAX_MOUNTS 16
#define VFS_MAX_OFT    256

/**
 * @brief Internal mount point descriptor.
 *
 * One slot per active mount.  The @c target string is normalised at mount
 * time so that ::vfs_find_mount can compare it directly against normalised
 * absolute paths.
 */
typedef struct
{
  char            target[VFS_PATH_MAX]; /**< Normalised absolute mount point. */
  void           *fs_data; /**< Volume-private data from @c mount callback. */
  const fs_ops_t *ops;     /**< Driver operations for this volume. */
  const fs_type_t *type;   /**< Registered type descriptor. */
  bool             active; /**< @c true when this slot holds a live mount. */
} vfs_mount_t;

static vfs_mount_t      mounts[VFS_MAX_MOUNTS];
static vfs_oft_entry_t  oft[VFS_MAX_OFT];

static const fs_type_t *fs_registry[8];
static u32              fs_registry_count = 0;

/** @brief Return @c true if @p path begins with @p prefix as a path component.
 */
static bool vfs_path_starts_with(const char *path, const char *prefix)
{
  if(prefix[0] == '/' && prefix[1] == '\0')
    return path[0] == '/';

  u64 plen = kstrlen(prefix);
  if(kstrncmp(path, prefix, plen) == 0)
    return path[plen] == '\0' || path[plen] == '/';
  return false;
}

/**
 * @brief Find the mount whose target is the longest prefix of @p path.
 *
 * Sets @p *rel_path to the portion of @p path after the mount target; it is
 * set to @c "/" when the path exactly equals the mount point.
 *
 * @param path      Normalised absolute path to look up.
 * @param rel_path  Out-pointer receiving the driver-relative path; may be @c
 * NULL.
 * @return Pointer to the best-matching mount, or @c NULL if none is active.
 */
static vfs_mount_t *vfs_find_mount(const char *path, const char **rel_path)
{
  vfs_mount_t *best     = NULL;
  u64          best_len = 0;

  for(u32 i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(!mounts[i].active)
      continue;
    u64 plen = kstrlen(mounts[i].target);
    if(vfs_path_starts_with(path, mounts[i].target) && plen >= best_len) {
      best     = &mounts[i];
      best_len = plen;
    }
  }

  if(best && rel_path) {
    *rel_path = path + best_len;
    if((*rel_path)[0] == '\0')
      *rel_path = "/";
  }
  return best;
}

/**
 * @brief Normalise an absolute path in-place.
 *
 * Collapses repeated slashes, resolves @c . components, and resolves @c ..
 * by stripping the preceding segment.  The root @c / is always preserved.
 *
 * @param path  Buffer of at least ::VFS_PATH_MAX bytes containing an
 *              absolute path.  Modified in-place.
 */
static void vfs_normalize(char *path)
{
  if(!path || path[0] != '/')
    return;

  char  res[VFS_PATH_MAX];
  char *out = res;
  *out++    = '/';

  const char *curr = path + 1;
  while(*curr) {
    while(*curr == '/')
      curr++;
    if(!*curr)
      break;

    const char *start = curr;
    while(*curr && *curr != '/')
      curr++;
    u64 len = (u64)(curr - start);

    if(len == 1 && start[0] == '.')
      continue;
    if(len == 2 && start[0] == '.' && start[1] == '.') {
      if(out > res + 1) {
        out--;
        while(out > res + 1 && *(out - 1) != '/')
          out--;
      }
      continue;
    }

    if(out > res + 1)
      *out++ = '/';
    kmemcpy(out, start, len);
    out += len;
  }
  *out = '\0';
  kstrncpy(path, res, VFS_PATH_MAX);
}

/**
 * @brief Build an absolute, normalised path in @p out.
 *
 * If @p path is relative it is appended to the calling process's CWD,
 * falling back to @c "/" when no process is running.  The result is always
 * normalised via ::vfs_normalize.
 *
 * @param path  Input path (absolute or relative).
 * @param out   Destination buffer of at least ::VFS_PATH_MAX bytes.
 */
static void vfs_make_absolute(const char *path, char *out)
{
  if(path[0] == '/') {
    kstrncpy(out, path, VFS_PATH_MAX);
  } else {
    proc_t     *p   = proc_current();
    const char *cwd = (p && p->cwd[0]) ? p->cwd : "/";
    kstrncpy(out, cwd, VFS_PATH_MAX);
    u64 len = kstrlen(out);
    if(out[len - 1] != '/') {
      out[len++] = '/';
      out[len]   = '\0';
    }
    kstrncat(out, path, VFS_PATH_MAX - len - 1);
  }
  vfs_normalize(out);
}

/** @brief Allocate and zero a free OFT slot; refcount is initialised to 1. */
static i32 oft_alloc(void)
{
  for(i32 i = 0; i < VFS_MAX_OFT; i++) {
    if(!oft[i].in_use) {
      kzero(&oft[i], sizeof(vfs_oft_entry_t));
      oft[i].in_use   = true;
      oft[i].refcount = 1;
      return i;
    }
  }
  return -ENFILE;
}

/**
 * @brief Translate a process fd number to its OFT index.
 *
 * @return OFT index on success, or @c -1 if @p fd is out of range, closed,
 *         or the OFT slot is no longer in use.
 */
static i32 fd_to_oft(i64 fd)
{
  proc_t *p = proc_current();
  if(!p || fd < 0 || fd >= VFS_MAX_FD)
    return -1;
  i32 idx = p->fds[fd];
  if(idx < 0 || idx >= VFS_MAX_OFT || !oft[idx].in_use)
    return -1;
  return idx;
}

/** @brief Increment the OFT refcount for slot @p idx. */
void vfs_oft_retain(i32 idx)
{
  if(idx >= 0 && idx < VFS_MAX_OFT && oft[idx].in_use)
    oft[idx].refcount++;
}

/**
 * @brief Decrement the OFT refcount for slot @p idx.
 *
 * When the count reaches zero the entry is torn down: ::pipe_oft_release is
 * called for pipe entries; the driver's @c close is called for file entries.
 */
void vfs_oft_release(i32 idx)
{
  if(idx < 0 || idx >= VFS_MAX_OFT || !oft[idx].in_use)
    return;

  if(--oft[idx].refcount > 0)
    return;

  if(oft[idx].pipe)
    pipe_oft_release(oft[idx].kind, oft[idx].pipe);

  if(oft[idx].handle && oft[idx].ops && oft[idx].ops->close)
    oft[idx].ops->close(oft[idx].handle);

  oft[idx].in_use = false;
}

/**
 * @brief Install a new fd in the calling process pointing at OFT slot @p
 * oft_idx.
 *
 * Searches for the lowest free fd ≥ 3 (slots 0–2 are reserved for stdio).
 * Does not increment the OFT refcount — the caller owns that responsibility.
 *
 * @return New fd on success, @c -EMFILE if the process fd table is full.
 */
i64 vfs_install_fd(i32 oft_idx)
{
  proc_t *p = proc_current();
  if(!p)
    return -EINVAL;
  for(i64 i = 3; i < VFS_MAX_FD; i++) {
    if(p->fds[i] < 0) {
      p->fds[i] = oft_idx;
      return i;
    }
  }
  return -EMFILE;
}

/** @brief Zero the mount table and OFT. */
void vfs_init(void)
{
  kzero(mounts, sizeof(mounts));
  kzero(oft, sizeof(oft));
}

/** @brief Register a filesystem driver in the type registry. */
i64 vfs_register_fs(const fs_type_t *fstype)
{
  if(fs_registry_count >= 8)
    return -ENOMEM;
  fs_registry[fs_registry_count++] = fstype;
  return 0;
}

/**
 * @brief Mount @p source at @p target using the named filesystem driver.
 *
 * Looks up @p fstype in the driver registry, invokes its @c mount callback to
 * obtain the volume-private pointer, and records the mount in the first free
 * slot.  The mount-point path is normalised before storage.
 */
i64 vfs_mount(const char *source, const char *target, const char *fstype)
{
  const fs_type_t *type = NULL;
  for(u32 i = 0; i < fs_registry_count; i++) {
    if(kstreq(fs_registry[i]->name, fstype)) {
      type = fs_registry[i];
      break;
    }
  }
  if(!type)
    return -ENODEV;

  i32 slot = -1;
  for(i32 i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(!mounts[i].active) {
      slot = i;
      break;
    }
  }
  if(slot < 0)
    return -ENOMEM;

  void *fs_data = type->mount(source, 0);
  if(!fs_data)
    return -EINVAL;

  mounts[slot].active  = true;
  mounts[slot].type    = type;
  mounts[slot].ops     = type->ops;
  mounts[slot].fs_data = fs_data;
  kstrncpy(mounts[slot].target, target, VFS_PATH_MAX);
  vfs_normalize(mounts[slot].target);

  return 0;
}

/**
 * @brief Open or create a file and install a file descriptor.
 *
 * Resolves @p path to an absolute form, dispatches to the responsible driver,
 * allocates an OFT entry, and returns the lowest available fd ≥ 3.
 */
i64 vfs_open(const char *path, u32 flags)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);

  const char  *rel   = NULL;
  vfs_mount_t *mount = vfs_find_mount(abs, &rel);
  if(!mount)
    return -ENOENT;

  fs_handle_t fh = mount->ops->open(mount->fs_data, rel, flags);
  if(!fh)
    return -ENOENT;

  i32 oft_idx = oft_alloc();
  if(oft_idx < 0) {
    mount->ops->close(fh);
    return oft_idx;
  }

  oft[oft_idx].handle = fh;
  oft[oft_idx].ops    = mount->ops;
  oft[oft_idx].flags  = flags;
  oft[oft_idx].kind   = VFS_KIND_FILE;
  oft[oft_idx].offset = 0;

  i64 fd = vfs_install_fd(oft_idx);
  if(fd < 0) {
    vfs_oft_release(oft_idx);
    return fd;
  }

  proc_t *p = proc_current();
  if(p)
    p->fd_cloexec[fd] = (flags & O_CLOEXEC) ? 1 : 0;

  return fd;
}

/**
 * @brief Read up to @p count bytes from @p fd into @p buf.
 *
 * Pipe read-ends block until data is available or the write end closes.
 * The OFT file offset is advanced by the number of bytes actually read.
 */
i64 vfs_read(i64 fd, void *buf, u64 count)
{
  i32 oft_idx = fd_to_oft(fd);
  if(oft_idx < 0)
    return -EBADF;

  vfs_oft_entry_t *e = &oft[oft_idx];
  if(e->kind == VFS_KIND_PIPE_RD)
    return pipe_read_obj(e->pipe, buf, count);
  if(e->kind == VFS_KIND_PIPE_WR)
    return -EBADF;

  i64 bytes = e->ops->read(e->handle, buf, count, e->offset);
  if(bytes > 0)
    e->offset += (u64)bytes;
  return bytes;
}

/**
 * @brief Write @p count bytes from @p buf to @p fd.
 *
 * If @c O_APPEND is set the offset is moved to end-of-file before writing.
 * Pipe write-ends block when the ring buffer is full.
 */
i64 vfs_write(i64 fd, const void *buf, u64 count)
{
  i32 oft_idx = fd_to_oft(fd);
  if(oft_idx < 0)
    return -EBADF;

  vfs_oft_entry_t *e = &oft[oft_idx];
  if(e->kind == VFS_KIND_PIPE_WR)
    return pipe_write_obj(e->pipe, buf, count);
  if(e->kind == VFS_KIND_PIPE_RD)
    return -EBADF;

  if(e->flags & O_APPEND) {
    vfs_stat_t st;
    if(e->ops->fstat(e->handle, &st) == 0)
      e->offset = st.size;
  }

  i64 bytes = e->ops->write(e->handle, buf, count, e->offset);
  if(bytes > 0)
    e->offset += (u64)bytes;
  return bytes;
}

/**
 * @brief Release a file descriptor and decrement the OFT refcount.
 *
 * The fd slot is cleared in the calling process's table.  The underlying OFT
 * entry is destroyed only when its refcount reaches zero.
 */
i64 vfs_close(i64 fd)
{
  proc_t *p = proc_current();
  if(!p || fd < 0 || fd >= VFS_MAX_FD)
    return -EBADF;
  i32 oft_idx = p->fds[fd];
  if(oft_idx < 0)
    return -EBADF;

  vfs_oft_release(oft_idx);
  p->fds[fd]        = -1;
  p->fd_cloexec[fd] = 0;
  return 0;
}

/** @brief Stat the node at @p path via the responsible driver. */
i64 vfs_stat(const char *path, vfs_stat_t *st)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  const char  *rel   = NULL;
  vfs_mount_t *mount = vfs_find_mount(abs, &rel);
  if(!mount)
    return -ENOENT;
  return mount->ops->stat(mount->fs_data, rel, st);
}

/** @brief Stat an open file descriptor, returning synthetic metadata for pipes.
 */
i64 vfs_fstat(i64 fd, vfs_stat_t *st)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  if(oft[idx].pipe) {
    kzero(st, sizeof(*st));
    st->type = VFS_FIFO;
    return 0;
  }
  return oft[idx].ops->fstat(oft[idx].handle, st);
}

/** @brief Create a directory at @p path via the responsible driver. */
i64 vfs_mkdir(const char *path)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  const char  *rel   = NULL;
  vfs_mount_t *mount = vfs_find_mount(abs, &rel);
  if(!mount)
    return -ENOENT;
  return mount->ops->mkdir(mount->fs_data, rel);
}

/** @brief Delete the file at @p path; fails if it is a directory. */
i64 vfs_unlink(const char *path)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  const char  *rel   = NULL;
  vfs_mount_t *mount = vfs_find_mount(abs, &rel);
  if(!mount)
    return -ENOENT;
  return mount->ops->unlink(mount->fs_data, rel);
}

/** @brief Remove the empty directory at @p path. */
i64 vfs_rmdir(const char *path)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  const char  *rel   = NULL;
  vfs_mount_t *mount = vfs_find_mount(abs, &rel);
  if(!mount)
    return -ENOENT;
  return mount->ops->rmdir(mount->fs_data, rel);
}

/**
 * @brief Reposition the file offset of @p fd.
 *
 * @p whence must be ::SEEK_SET, ::SEEK_CUR, or ::SEEK_END.  Seeking on a
 * pipe returns @c -ESPIPE.  A resulting negative offset returns @c -EINVAL.
 */
i64 vfs_seek(i64 fd, i64 offset, i32 whence)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  vfs_oft_entry_t *e = &oft[idx];

  if(e->pipe)
    return -ESPIPE;

  u64 base;
  switch(whence) {
  case SEEK_SET:
    base = 0;
    break;
  case SEEK_CUR:
    base = e->offset;
    break;
  case SEEK_END: {
    vfs_stat_t st;
    if(e->ops->fstat(e->handle, &st) < 0)
      return -EINVAL;
    base = st.size;
    break;
  }
  default:
    return -EINVAL;
  }

  i64 target = (i64)base + offset;
  if(target < 0)
    return -EINVAL;
  e->offset = (u64)target;
  return target;
}

/**
 * @brief Fill @p buf with @c dirent64 entries from an open directory fd.
 *
 * Reads as many complete entries as fit within @p count bytes, advancing the
 * OFT directory position on each successful entry.  Returns 0 when exhausted.
 */
i64 vfs_getdents(i64 fd, void *buf, u64 count)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;

  vfs_oft_entry_t *e       = &oft[idx];
  u8              *out     = (u8 *)buf;
  u64              written = 0;

  while(written + 32 <= count) {
    vfs_stat_t st;
    char       name[VFS_NAME_MAX + 1];
    i64        ret = e->ops->readdir(e->handle, e->offset, name, &st);
    if(ret <= 0)
      break;

    u32 namelen = (u32)kstrlen(name);
    u32 reclen  = (u32)((19 + namelen + 1 + 7) & ~7ULL);

    if(written + reclen > count)
      break;

    dirent_t *d = (dirent_t *)(out + written);
    d->d_ino    = st.ino;
    d->d_off    = (i64)(e->offset + 1);
    d->d_reclen = (u16)reclen;
    d->d_type   = (st.type == VFS_DIRECTORY) ? DT_DIR : DT_REG;
    kstrncpy(d->d_name, name, namelen + 1);

    written += reclen;
    e->offset++;
  }

  return (i64)written;
}

/**
 * @brief Change the calling process's working directory to @p path.
 *
 * The path is validated as an existing directory before updating
 * @c proc_t::cwd.  Returns @c -ENOTDIR if the target does not exist or is
 * not a directory.
 */
i64 vfs_chdir(const char *path)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  vfs_stat_t st;
  if(vfs_stat(abs, &st) < 0 || st.type != VFS_DIRECTORY)
    return -ENOTDIR;
  proc_t *p = proc_current();
  if(p)
    kstrncpy(p->cwd, abs, VFS_PATH_MAX);
  return 0;
}

/**
 * @brief Return a pointer to the calling process's current working directory.
 *
 * The pointer addresses storage inside the current @c proc_t.  Returns
 * @c "/" when no process is running (early-boot context).
 */
const char *vfs_getcwd(void)
{
  proc_t *p = proc_current();
  return p ? p->cwd : "/";
}

#define RENAME_CHUNK 4096

/**
 * @brief Rename (move) a file, copying across mount points if necessary.
 *
 * Opens @p oldpath and @p newpath directly through their driver handles to
 * avoid consuming process fd slots.  The copy-and-unlink is not atomic.
 * Directories and files larger than 16 MiB are rejected.
 */
i64 vfs_rename(const char *oldpath, const char *newpath)
{
  char abs_old[VFS_PATH_MAX];
  char abs_new[VFS_PATH_MAX];
  vfs_make_absolute(oldpath, abs_old);
  vfs_make_absolute(newpath, abs_new);

  vfs_stat_t st;
  if(vfs_stat(abs_old, &st) < 0)
    return -ENOENT;
  if(st.type == VFS_DIRECTORY)
    return -EISDIR;
  if(st.size > 16ULL * 1024 * 1024)
    return -ENOSYS;

  const char  *src_rel, *dst_rel;
  vfs_mount_t *src_m = vfs_find_mount(abs_old, &src_rel);
  vfs_mount_t *dst_m = vfs_find_mount(abs_new, &dst_rel);
  if(!src_m || !dst_m)
    return -ENOENT;

  fs_handle_t src_fh = src_m->ops->open(src_m->fs_data, src_rel, O_RDONLY);
  if(!src_fh)
    return -ENOENT;

  fs_handle_t dst_fh =
      dst_m->ops->open(dst_m->fs_data, dst_rel, O_WRONLY | O_CREAT | O_TRUNC);
  if(!dst_fh) {
    src_m->ops->close(src_fh);
    return -EIO;
  }

  static u8 rename_buf[RENAME_CHUNK];
  u64       off = 0;
  i64       n;
  while((n = src_m->ops->read(src_fh, rename_buf, RENAME_CHUNK, off)) > 0) {
    dst_m->ops->write(dst_fh, rename_buf, (u64)n, off);
    off += (u64)n;
  }

  src_m->ops->close(src_fh);
  dst_m->ops->close(dst_fh);
  src_m->ops->unlink(src_m->fs_data, src_rel);
  return 0;
}

/** @brief Read the target of the symbolic link at @p path into @p buf. */
i64 vfs_readlink(const char *path, char *buf, u64 cap)
{
  char abs[VFS_PATH_MAX];
  vfs_make_absolute(path, abs);
  const char  *rel;
  vfs_mount_t *m = vfs_find_mount(abs, &rel);
  if(!m || !m->ops->readlink)
    return -ENOSYS;
  return m->ops->readlink(m->fs_data, rel, buf, cap);
}

/** @brief Truncate @p fd to exactly @p length bytes via the driver. */
i64 vfs_ftruncate(i64 fd, u64 length)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  if(oft[idx].pipe)
    return -EINVAL;
  return oft[idx].ops->truncate(oft[idx].handle, length);
}

/** @brief Return the open flags stored in the OFT for @p fd. */
i64 vfs_get_flags(i64 fd)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  return oft[idx].flags;
}

/** @brief Overwrite the open flags for @p fd (used by @c fcntl @c F_SETFL). */
i64 vfs_set_flags(i64 fd, u32 flags)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  oft[idx].flags = flags;
  return 0;
}

/** @brief Duplicate @p oldfd into the lowest free fd ≥ 3 and retain the OFT
 * entry. */
i64 vfs_dup(i64 oldfd)
{
  i32 idx = fd_to_oft(oldfd);
  if(idx < 0)
    return -EBADF;
  i64 newfd = vfs_install_fd(idx);
  if(newfd >= 0)
    vfs_oft_retain(idx);
  return newfd;
}

/**
 * @brief Duplicate @p oldfd into the specific slot @p newfd.
 *
 * If @p newfd is already open it is silently closed first.  If @p oldfd
 * equals @p newfd the call is a no-op.
 */
i64 vfs_dup2(i64 oldfd, i64 newfd)
{
  if(oldfd == newfd)
    return newfd;
  i32 idx = fd_to_oft(oldfd);
  if(idx < 0)
    return -EBADF;
  proc_t *p = proc_current();
  if(newfd < 0 || newfd >= VFS_MAX_FD)
    return -EBADF;
  if(p->fds[newfd] >= 0)
    vfs_close(newfd);
  p->fds[newfd] = idx;
  vfs_oft_retain(idx);
  return newfd;
}

/** @brief Return positive if @p fd has data available without blocking, 0 if
 * not ready. */
i32 vfs_select_read_ready(i64 fd)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  if(oft[idx].kind == VFS_KIND_PIPE_WR)
    return -EBADF;
  if(oft[idx].kind == VFS_KIND_PIPE_RD)
    return pipe_poll_read_ready(oft[idx].pipe) ? 1 : 0;
  return 1;
}

/** @brief Return positive if @p fd can accept a write without blocking, 0 if
 * not ready. */
i32 vfs_select_write_ready(i64 fd)
{
  i32 idx = fd_to_oft(fd);
  if(idx < 0)
    return -EBADF;
  if(oft[idx].kind == VFS_KIND_PIPE_RD)
    return -EBADF;
  if(oft[idx].kind == VFS_KIND_PIPE_WR)
    return pipe_poll_write_ready(oft[idx].pipe) ? 1 : 0;
  return 1;
}

/** @brief Return @c true if @p fd refers to either end of a pipe. */
bool vfs_fd_is_pipe(u64 fd)
{
  i32 idx = fd_to_oft((i64)fd);
  if(idx < 0)
    return false;
  return oft[idx].kind == VFS_KIND_PIPE_RD || oft[idx].kind == VFS_KIND_PIPE_WR;
}

/** @brief Return @c true if @p fd is currently open in the calling process. */
bool vfs_fd_is_valid(i64 fd)
{
  return fd_to_oft(fd) >= 0;
}

/** @brief Allocate an OFT entry for one end of a pipe and set its kind. */
i32 vfs_oft_alloc_pipe(i32 kind, void *pipe)
{
  i32 idx = oft_alloc();
  if(idx < 0)
    return idx;
  oft[idx].kind = kind;
  oft[idx].pipe = pipe;
  return idx;
}

/** @brief Initialise @p fds to the "all closed" state (-1 in every slot). */
void vfs_proc_init_fds(i32 *fds)
{
  for(int i = 0; i < VFS_MAX_FD; i++)
    fds[i] = -1;
}

/**
 * @brief Copy the parent's fd table into the child's after @c fork.
 *
 * Retains every open OFT entry so parent and child hold independent
 * references to the same descriptions.
 */
void vfs_proc_inherit_fds(
    i32 *child_fds, u8 *child_clox, const i32 *parent_fds, const u8 *parent_clox
)
{
  for(int i = 0; i < VFS_MAX_FD; i++) {
    child_fds[i]  = parent_fds[i];
    child_clox[i] = parent_clox[i];
    if(child_fds[i] >= 0)
      vfs_oft_retain(child_fds[i]);
  }
}

/**
 * @brief Release all file descriptors held by an exiting process.
 *
 * Calls ::vfs_oft_release for every open slot and sets each entry to -1.
 * OFT entries are destroyed only when their refcount reaches zero.
 */
void vfs_proc_release_fds(i32 *fds)
{
  for(int i = 0; i < VFS_MAX_FD; i++) {
    if(fds[i] >= 0) {
      vfs_oft_release(fds[i]);
      fds[i] = -1;
    }
  }
}

/**
 * @brief Close every fd in the calling process that has @c FD_CLOEXEC set.
 *
 * Called by the @c execve path after the new image is loaded, before waking
 * any vfork-blocked parent.
 */
void vfs_proc_close_cloexec_fds(void)
{
  proc_t *p = proc_current();
  if(!p)
    return;
  for(int i = 0; i < VFS_MAX_FD; i++) {
    if(p->fd_cloexec[i] && p->fds[i] >= 0) {
      vfs_oft_release(p->fds[i]);
      p->fds[i]        = -1;
      p->fd_cloexec[i] = 0;
    }
  }
}
