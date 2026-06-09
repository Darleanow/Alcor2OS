/**
 * @file src/kernel/sys/sys_fs.c
 * @brief Filesystem syscalls: open, close, stat, seek, rename, chdir, …
 *
 * Every entry point validates user pointers through the VMM before touching
 * them, then delegates to the VFS layer.  The @c stat_buf layout follows the
 * x86-64 POSIX ABI used by @c stat / @c fstat / @c lstat.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/pipe.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

/**
 * @brief POSIX @c stat buffer layout for the x86-64 syscall ABI.
 *
 * Filled by ::fill_stat_buf from a ::vfs_stat_t before copying to
 * user space.  Field order and sizes follow the x86-64 POSIX ABI.
 */
struct stat_buf
{
  u64 st_dev;
  u64 st_ino;
  u64 st_nlink;
  u32 st_mode;
  u32 st_uid;
  u32 st_gid;
  u32 pad0;
  u64 st_rdev;
  i64 st_size;
  i64 st_blksize;
  i64 st_blocks;
  u64 st_atime;
  u64 st_atime_nsec;
  u64 st_mtime;
  u64 st_mtime_nsec;
  u64 st_ctime;
  u64 st_ctime_nsec;
  i64 unused[3];
};

#define S_IFIFO 0010000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000

/** @brief Return non-zero if @p path refers to the @c /proc/self/exe virtual
 * symlink. */
static int path_is_proc_self_exe(const char *path)
{
  return !kstrcmp(path, "/proc/self/exe") ||
         !kstrcmp(path, "/proc/thread-self/exe");
}

/** @brief Fill @p st with synthetic stat data for the @c /proc/self/exe virtual
 * symlink. */
static void fill_proc_self_exe_stat(struct stat_buf *st, proc_t *p)
{
  kzero(st, sizeof(*st));
  st->st_dev     = 0x0000002000000000ULL;
  st->st_ino     = p ? p->pid : 1;
  st->st_nlink   = 1;
  st->st_mode    = S_IFLNK | 0777;
  st->st_uid     = 0;
  st->st_gid     = 0;
  st->st_size    = (i64)((p && p->exe_path[0]) ? kstrlen(p->exe_path) : 0);
  st->st_blksize = 4096;
}

/** @brief Return @c true if @p ptr is a valid, non-NULL user-space pointer. */
static inline bool user_cstr_ok(u64 ptr)
{
  return ptr && vmm_is_user_ptr((void *)ptr);
}

/** @brief Return @c true if @p ptr..@p ptr+size is a valid user-space range. */
static inline bool user_buf_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

/**
 * @brief Translate an internal ::vfs_stat_t into the POSIX @c stat layout.
 *
 * Files are reported as user-executable (@c 0755) even without real mode
 * bits - toolchain binaries rely on this for @c posix_spawn permission checks.
 */
static void fill_stat_buf(struct stat_buf *st, const vfs_stat_t *vst)
{
  kzero(st, sizeof(*st));
  st->st_dev     = vst->dev;
  st->st_ino     = vst->ino;
  st->st_nlink   = 1;
  st->st_mode    = (vst->type == VFS_DIRECTORY) ? (S_IFDIR | 0755)
                   : (vst->type == VFS_FIFO)    ? (S_IFIFO | 0666)
                                                : (S_IFREG | 0755);
  st->st_size    = (i64)vst->size;
  st->st_blksize = 4096;
  st->st_blocks  = ((i64)vst->size + 511) / 512;
}

/** @brief Open or create a file and return a file descriptor. */
kern_err_t sys_open(u64 path, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return -EFAULT;

  return vfs_open((const char *)path, (u32)flags);
}

/** @brief Release @p fd and decrement its OFT refcount. */
kern_err_t sys_close(u64 fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  return vfs_close((i64)fd);
}

/** @brief Stat the node at @p path into a POSIX @c stat buffer. */
kern_err_t sys_stat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path) || !user_buf_ok(statbuf, sizeof(struct stat_buf)))
    return -EFAULT;

  if(path_is_proc_self_exe((const char *)path)) {
    fill_proc_self_exe_stat((struct stat_buf *)statbuf, proc_current());
    return 0;
  }

  vfs_stat_t    vst;
  kern_err_t rc = vfs_stat((const char *)path, &vst);
  if(rc < 0)
    return rc;

  fill_stat_buf((struct stat_buf *)statbuf, &vst);
  return 0;
}

/**
 * @brief Stat the open file descriptor @p fd into a POSIX @c stat buffer.
 *
 * FDs 0–2 (stdio) return synthetic character-device metadata since they have
 * no OFT entry in the default configuration.
 */
kern_err_t sys_fstat(u64 fd, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(statbuf, sizeof(struct stat_buf)))
    return -EFAULT;

  struct stat_buf *st = (struct stat_buf *)statbuf;
  if(fd <= 2) {
    kzero(st, sizeof(*st));
    st->st_dev     = 0x1000000000000000ULL | (u64)fd;
    st->st_ino     = (u64)fd;
    st->st_mode    = 0020000 | 0666;
    st->st_blksize = 4096;
    st->st_nlink   = 1;
    return 0;
  }

  vfs_stat_t vst;
  kern_err_t rc = vfs_fstat((i64)fd, &vst);
  if(rc < 0)
    return rc;

  fill_stat_buf(st, &vst);
  return 0;
}

/** @brief @c lstat - no symlink resolution in VFS yet; identical to @c stat. */
kern_err_t sys_lstat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_stat(path, statbuf, a3, a4, a5, a6);
}

/** @brief Check that @p path exists and is accessible; always grants access. */
kern_err_t sys_access(u64 path, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return -EFAULT;

  if(path_is_proc_self_exe((const char *)path)) {
    const proc_t *p = proc_current();
    if(!p || !p->exe_path[0])
      return -ENOENT;
    return 0;
  }

  vfs_stat_t st;
  return vfs_stat((const char *)path, &st);
}

/**
 * @brief @c faccessat - only @c AT_FDCWD and absolute paths are supported.
 *
 * Relative paths with a real @p dirfd return @c -ENOSYS; all supported flags
 * are accepted and silently ignored (no fine-grained mode checking in VFS).
 */
kern_err_t sys_faccessat(u64 dirfd, u64 pathname, u64 mode, u64 flags, u64 a5, u64 a6)
{
  const i64 AT_FDCWD = -100;

  (void)a5;
  (void)a6;
  (void)flags;

  if(!user_cstr_ok(pathname))
    return -EFAULT;

  const char *p = (const char *)pathname;
  if(p[0] != '/' && (i64)dirfd != AT_FDCWD)
    return -ENOSYS;

  return sys_access(pathname, mode, 0, 0, 0, 0);
}

/**
 * @brief @c newfstatat - supports @c AT_FDCWD and absolute paths only.
 *
 * @c AT_SYMLINK_NOFOLLOW is accepted but not enforced (VFS has no @c lstat).
 * @c AT_EMPTY_PATH returns @c -ENOSYS.
 */
kern_err_t sys_newfstatat(
    u64 dirfd, u64 pathname, u64 statbuf, u64 flags, u64 a5, u64 a6
)
{
  const i64 AT_FDCWD            = -100;
  const u32 AT_SYMLINK_NOFOLLOW = 0x100u;
  const u32 AT_NO_AUTOMOUNT     = 0x800u;
  const u32 AT_EMPTY_PATH       = 0x1000u;
  const u32 AT_STATX_MASK       = 0x6000u;

  (void)a5;
  (void)a6;

  if(flags & AT_EMPTY_PATH)
    return -ENOSYS;

  {
    u32 allowed = AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT | AT_STATX_MASK;
    if((u32)flags & ~allowed)
      return -EINVAL;
  }

  if(!user_cstr_ok(pathname) || !user_buf_ok(statbuf, sizeof(struct stat_buf)))
    return -EFAULT;

  const char *p = (const char *)pathname;
  if(p[0] != '/' && (i64)dirfd != AT_FDCWD)
    return -ENOSYS;

  (void)dirfd;
  (void)flags;
  return sys_stat(pathname, statbuf, 0, 0, 0, 0);
}

/** @brief Duplicate @p oldfd to the lowest free fd ≥ 3. */
kern_err_t sys_dup(u64 oldfd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  return vfs_dup((i64)oldfd);
}

/** @brief Duplicate @p oldfd into the specific slot @p newfd. */
kern_err_t sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(oldfd == newfd)
    return newfd;

  return vfs_dup2((i64)oldfd, (i64)newfd);
}

#define F_DUPFD    0
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4
#define FD_CLOEXEC 1

/**
 * @brief Perform a file-control operation on @p fd.
 *
 * Supported commands: @c F_DUPFD, @c F_GETFD, @c F_SETFD, @c F_GETFL,
 * @c F_SETFL.  Unknown commands return 0 to avoid breaking musl probes.
 */
kern_err_t sys_fcntl(u64 fd, u64 cmd, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  switch((int)cmd) {
  case F_DUPFD:
    if(fd <= 2)
      return fd;
    {
      i64 result = vfs_dup((i64)fd);
      return (result < 0) ? -EBADF : result;
    }
  case F_GETFD: {
    if((i64)fd < 0 || (i64)fd >= VFS_MAX_FD)
      return -EBADF;
    proc_t *p = proc_current();
    if(!p)
      return -EINVAL;
    return p->fd_cloexec[fd] ? FD_CLOEXEC : 0;
  }
  case F_SETFD: {
    if((i64)fd < 0 || (i64)fd >= VFS_MAX_FD)
      return -EBADF;
    proc_t *p = proc_current();
    if(!p)
      return -EINVAL;
    if(fd > 2 && fd < 256 && p->fds[fd] < 0)
      return -EBADF;
    p->fd_cloexec[fd] = (arg & FD_CLOEXEC) ? 1 : 0;
    return 0;
  }
  case F_GETFL:
    if(fd <= 2)
      return O_RDWR;
    {
      i64 flags = vfs_get_flags((i64)fd);
      return (flags < 0) ? -EBADF : flags;
    }
  case F_SETFL:
    if(fd <= 2)
      return 0;
    return vfs_set_flags((i64)fd, (u32)arg) < 0 ? -EBADF : 0;
  default:
    return 0;
  }
}

/** @brief Fill @p dirp with @c dirent64 entries from the open directory @p fd.
 */
kern_err_t sys_getdents(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(dirp, count))
    return -EFAULT;
  if(count < 32)
    return -EINVAL;

  i64 result = vfs_getdents((i64)fd, (void *)dirp, count);
  return result;
}

/** @brief @c getdents64 - identical to ::sys_getdents on this platform. */
kern_err_t sys_getdents64(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  return sys_getdents(fd, dirp, count, a4, a5, a6);
}

/** @brief Copy the calling process's CWD string into the user buffer @p buf. */
kern_err_t sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
    return -EFAULT;
  if(size == 0)
    return -EINVAL;
  if(!user_buf_ok(buf, size))
    return -EFAULT;

  const char *cwd = vfs_getcwd();
  u64         len = kstrlen(cwd);
  if(len + 1 > size)
    return -ERANGE;
  kstrncpy((char *)buf, cwd, size);
  return len + 1;
}

/** @brief Change the calling process's working directory to @p path. */
kern_err_t sys_chdir(u64 path, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return -EFAULT;
  return vfs_chdir((const char *)path);
}

/** @brief Create a directory at @p pathname. */
kern_err_t sys_mkdir(u64 pathname, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return -EFAULT;
  return vfs_mkdir((const char *)pathname);
}

/** @brief Remove the empty directory at @p pathname. */
kern_err_t sys_rmdir(u64 pathname, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return -EFAULT;
  i64 result = vfs_rmdir((const char *)pathname);
  return (result < 0) ? result : 0;
}

/** @brief Create or truncate a file at @p pathname (@c open with @c
 * O_WRONLY|O_CREAT|O_TRUNC). */
kern_err_t sys_creat(u64 pathname, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return sys_open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode, 0, 0, 0);
}

/** @brief Delete the file at @p pathname. */
kern_err_t sys_unlink(u64 pathname, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return -EFAULT;
  return vfs_unlink((const char *)pathname);
}

/** @brief Rename @p oldpath to @p newpath, copying across mount points if
 * necessary. */
kern_err_t sys_rename(u64 oldpath, u64 newpath, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(oldpath) || !user_cstr_ok(newpath))
    return -EFAULT;

  i64 result = vfs_rename((const char *)oldpath, (const char *)newpath);
  return (result < 0) ? result : 0;
}

/** @brief Truncate the file open as @p fd to exactly @p length bytes. */
kern_err_t sys_ftruncate(u64 fd, u64 length, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd <= 2)
    return -EBADF;

  i64 result = vfs_ftruncate((i64)fd, (i64)length);
  return (result < 0) ? result : 0;
}

/**
 * @brief Read @p count bytes from @p fd at absolute @p offset without moving
 * the seek position.
 *
 * Saves and restores the OFT offset around the read; returns @c -ESPIPE if
 * the offset cannot be saved (e.g. pipe).
 */
kern_err_t sys_pread64(u64 fd, u64 buf, u64 count, u64 offset, u64 a5, u64 a6)
{
  (void)a5;
  (void)a6;

  if(!user_buf_ok(buf, count))
    return -EFAULT;
  i64 saved = vfs_seek((i64)fd, 0, SEEK_CUR);
  if(saved < 0)
    return saved;
  vfs_seek((i64)fd, (i64)offset, SEEK_SET);
  i64 result = vfs_read((i64)fd, (void *)buf, count);
  vfs_seek((i64)fd, saved, SEEK_SET);
  return result;
}

/**
 * @brief Write @p count bytes to @p fd at absolute @p offset without moving the
 * seek position.
 *
 * Stdio fds (0–2) delegate to ::sys_write since they have no seekable OFT
 * entry.  For regular files, the OFT offset is saved and restored.
 */
kern_err_t sys_pwrite64(u64 fd, u64 buf, u64 count, u64 offset, u64 a5, u64 a6)
{
  (void)a5;
  (void)a6;

  if(!user_buf_ok(buf, count))
    return -EFAULT;
  if(fd <= 2)
    return sys_write(fd, buf, count, 0, 0, 0);

  i64 saved = vfs_seek((i64)fd, 0, SEEK_CUR);
  if(saved < 0)
    return saved;
  vfs_seek((i64)fd, (i64)offset, SEEK_SET);
  i64 result = vfs_write((i64)fd, (const void *)buf, count);
  vfs_seek((i64)fd, saved, SEEK_SET);
  return result;
}

/** @brief @c symlink - not implemented; returns @c -ENOSYS. */
kern_err_t sys_symlink(u64 target, u64 linkpath, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)target;
  (void)linkpath;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return -ENOSYS;
}

/** @brief @c openat - only @c AT_FDCWD is supported; delegates to ::sys_open.
 */
kern_err_t sys_openat(u64 dirfd, u64 path, u64 flags, u64 mode, u64 a5, u64 a6)
{
  const i64 AT_FDCWD = -100;
  if((i64)dirfd != AT_FDCWD)
    return -ENOSYS;
  return sys_open(path, flags, mode, 0, a5, a6);
}

/**
 * @brief Read the target of a symbolic link at @p path into @p buf.
 *
 * @c /proc/self/exe is handled as a virtual symlink pointing to
 * @c proc_t::exe_path.  Other paths are forwarded to the VFS driver.
 */
kern_err_t sys_readlink(u64 path, u64 buf, u64 bufsiz, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path) || !user_buf_ok(buf, bufsiz))
    return -EFAULT;
  if(bufsiz == 0)
    return -EINVAL;

  const char *pstr = (const char *)path;

  if(path_is_proc_self_exe(pstr)) {
    const proc_t *p = proc_current();
    if(!p || !p->exe_path[0])
      return -ENOENT;
    u64 len = kstrlen(p->exe_path);
    if(len >= bufsiz)
      return -ERANGE;
    kmemcpy((void *)buf, p->exe_path, len);
    return len;
  }

  char ktarget[VFS_PATH_MAX];
  i64  tlen = vfs_readlink(pstr, ktarget, sizeof(ktarget));
  if(tlen < 0)
    return tlen;
  if((u64)tlen >= bufsiz)
    return -ERANGE;
  kmemcpy((void *)buf, ktarget, (u64)tlen);
  return tlen;
}

kern_err_t sys_pipe(u64 pipefd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!pipefd)
    return -EFAULT;
  if(!vmm_is_user_range((void *)pipefd, sizeof(int) * 2))
    return -EFAULT;
  if(!proc_current())
    return -EINVAL;

  void *pipe = pipe_alloc_obj();
  if(!pipe)
    return -ENOMEM;

  i32 read_oft = vfs_oft_alloc_pipe(VFS_KIND_PIPE_RD, pipe);
  if(read_oft < 0) {
    pipe_rd_release(pipe);
    pipe_wr_release(pipe);
    return -ENFILE;
  }
  i32 write_oft = vfs_oft_alloc_pipe(VFS_KIND_PIPE_WR, pipe);
  if(write_oft < 0) {
    vfs_oft_release(read_oft);
    return -ENFILE;
  }

  i64 read_fd = vfs_install_fd(read_oft);
  if(read_fd < 0) {
    vfs_oft_release(read_oft);
    vfs_oft_release(write_oft);
    return read_fd;
  }
  i64 write_fd = vfs_install_fd(write_oft);
  if(write_fd < 0) {
    vfs_oft_release(write_oft);
    proc_current()->fds[read_fd] = -1;
    vfs_oft_release(read_oft);
    return write_fd;
  }

  int *fds = (int *)pipefd;
  fds[0]   = (int)read_fd;
  fds[1]   = (int)write_fd;
  return 0;
}

kern_err_t sys_pipe2(u64 pipefd, u64 flags, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  u64 rc = sys_pipe(pipefd, 0, 0, 0, 0, 0);
  if(rc != 0)
    return rc;

  /* Apply O_CLOEXEC per-fd so exec auto-closes these ends (musl posix_spawn
   * relies on this for its error-reporting pipe).  FD_CLOEXEC is a per-fd
   * attribute - it must NOT be stored in the shared OFT entry. */
  if((u32)flags & O_CLOEXEC) {
    const int *fds = (const int *)pipefd;
    proc_t    *p   = proc_current();
    if(p) {
      p->fd_cloexec[fds[0]] = 1;
      p->fd_cloexec[fds[1]] = 1;
    }
  }
  return 0;
}
