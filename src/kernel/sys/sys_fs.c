/**
 * @file src/kernel/sys/sys_fs.c
 * @brief Path and file syscalls: open/stat/chdir/… into the VFS layer.
 *
 * `linux_stat` layout matches what musl/Linux binaries expect for `stat` / `fstat` / `lstat`.
 * User pointers are checked before copying into user space.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/sys/internal.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/mm/vmm.h>

struct linux_stat
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

#define S_IFDIR 0040000
#define S_IFREG 0100000

static inline bool user_cstr_ok(u64 ptr)
{
  return ptr && vmm_is_user_ptr((void *)ptr);
}

static inline bool user_buf_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

static void fill_linux_stat(struct linux_stat *st, const vfs_stat_t *vst, u64 ino)
{
  kzero(st, sizeof(*st));
  st->st_ino = ino;
  st->st_nlink = 1;
  st->st_mode = (vst->type == VFS_DIRECTORY) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  st->st_size = (i64)vst->size;
  st->st_blksize = 4096;
  st->st_blocks = ((i64)vst->size + 511) / 512;
}

u64 sys_open(u64 path, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return (u64)-EFAULT;

  i64 fd = vfs_open((const char *)path, (u32)flags);
  return (u64)fd;
}

u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd <= 2)
    return 0;
  if(pipe_close((int)fd) == 0)
    return 0;

  i64 result = vfs_close((i64)fd);
  return (result < 0) ? (u64)-EBADF : 0;
}

u64 sys_stat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path) || !user_buf_ok(statbuf, sizeof(struct linux_stat)))
    return (u64)-EFAULT;

  vfs_stat_t vst;
  if(vfs_stat((const char *)path, &vst) < 0)
    return (u64)-ENOENT;

  fill_linux_stat((struct linux_stat *)statbuf, &vst, 1);
  return 0;
}

u64 sys_fstat(u64 fd, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(statbuf, sizeof(struct linux_stat)))
    return (u64)-EFAULT;

  struct linux_stat *st = (struct linux_stat *)statbuf;
  if(fd <= 2) {
    kzero(st, sizeof(*st));
    st->st_mode    = 0020000 | 0666;
    st->st_blksize = 4096;
    st->st_nlink   = 1;
    return 0;
  }

  vfs_stat_t vst;
  if(vfs_fstat_fd((i64)fd, &vst) < 0)
    return (u64)-EBADF;

  fill_linux_stat(st, &vst, fd);
  return 0;
}

u64 sys_lstat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_stat(path, statbuf, a3, a4, a5, a6);
}

u64 sys_access(u64 path, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return (u64)-EFAULT;

  vfs_stat_t st;
  if(vfs_stat((const char *)path, &st) < 0)
    return (u64)-ENOENT;
  return 0;
}

u64 sys_dup(u64 oldfd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(oldfd <= 2)
    return oldfd;

  i64 result = vfs_dup((i64)oldfd);
  return (result < 0) ? (u64)-EBADF : (u64)result;
}

u64 sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(oldfd == newfd)
    return newfd;
  if(oldfd <= 2 || newfd <= 2)
    return newfd;

  i64 result = vfs_dup2((i64)oldfd, (i64)newfd);
  return (result < 0) ? (u64)-EBADF : (u64)result;
}

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

u64 sys_fcntl(u64 fd, u64 cmd, u64 arg, u64 a4, u64 a5, u64 a6)
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
      return (result < 0) ? (u64)-EBADF : (u64)result;
    }
  case F_GETFD:
    return 0;
  case F_SETFD:
    return 0;
  case F_GETFL:
    if(fd <= 2)
      return O_RDWR;
    {
      i64 flags = vfs_get_flags((i64)fd);
      return (flags < 0) ? (u64)-EBADF : (u64)flags;
    }
  case F_SETFL:
    if(fd <= 2)
      return 0;
    return vfs_set_flags((i64)fd, (u32)arg) < 0 ? (u64)-EBADF : 0;
  default:
    return 0;
  }
}

u64 sys_getdents(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(dirp, count))
    return (u64)-EFAULT;
  if(count < 32)
    return (u64)-EINVAL;

  i64 result = vfs_getdents((i64)fd, (void *)dirp, count);
  return (result < 0) ? (u64)-EBADF : (u64)result;
}

u64 sys_getdents64(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  return sys_getdents(fd, dirp, count, a4, a5, a6);
}

u64 sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
    return (u64)-EFAULT;
  if(size == 0)
    return (u64)-EINVAL;
  if(!user_buf_ok(buf, size))
    return (u64)-EFAULT;

  const char *cwd = vfs_getcwd();
  u64 len         = kstrlen(cwd);
  if(len + 1 > size)
    return (u64)-ERANGE;
  kstrncpy((char *)buf, cwd, size);
  return len + 1;
}

u64 sys_chdir(u64 path, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(path))
    return (u64)-EFAULT;
  i64 result = vfs_chdir((const char *)path);
  return (result < 0) ? (u64)-ENOENT : 0;
}

u64 sys_mkdir(u64 pathname, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return (u64)-EFAULT;
  i64 result = vfs_mkdir((const char *)pathname);
  return (result < 0) ? (u64)-ENOENT : 0;
}

u64 sys_rmdir(u64 pathname, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return (u64)-EFAULT;
  i64 result = vfs_rmdir((const char *)pathname);
  return (result < 0) ? (u64)result : 0;
}

u64 sys_creat(u64 pathname, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return sys_open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode, 0, 0, 0);
}

u64 sys_unlink(u64 pathname, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return (u64)-EFAULT;
  i64 result = vfs_unlink((const char *)pathname);
  return (result < 0) ? (u64)-ENOENT : 0;
}

#define RENAME_CHUNK 4096
static char rename_chunk[RENAME_CHUNK];

u64 sys_rename(u64 oldpath, u64 newpath, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(oldpath) || !user_cstr_ok(newpath))
    return (u64)-EFAULT;

  const char *old = (const char *)oldpath;
  const char *new = (const char *)newpath;

  vfs_stat_t st;
  if(vfs_stat(old, &st) < 0)
    return (u64)-ENOENT;
  if(st.type == VFS_DIRECTORY)
    return (u64)-EISDIR;
  if(st.size > 16ULL * 1024 * 1024)
    return (u64)-ENOSYS;

  i64 src = vfs_open(old, O_RDONLY);
  if(src < 0)
    return (u64)-ENOENT;

  i64 dst = vfs_open(new, O_WRONLY | O_CREAT | O_TRUNC);
  if(dst < 0) {
    vfs_close(src);
    return (u64)-EIO;
  }

  i64 n;
  while((n = vfs_read(src, rename_chunk, RENAME_CHUNK)) > 0)
    vfs_write(dst, rename_chunk, (u64)n);

  vfs_close(src);
  vfs_close(dst);
  vfs_unlink(old);
  return 0;
}

u64 sys_ftruncate(u64 fd, u64 length, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd <= 2)
    return (u64)-EBADF;

  i64 result = vfs_ftruncate((i64)fd, (i64)length);
  return (result < 0) ? (u64)result : 0;
}

u64 sys_pread64(u64 fd, u64 buf, u64 count, u64 offset, u64 a5, u64 a6)
{
  (void)a5;
  (void)a6;

  if(!user_buf_ok(buf, count))
    return (u64)-EFAULT;
  i64 saved = vfs_seek((i64)fd, 0, SEEK_CUR);
  vfs_seek((i64)fd, (i64)offset, SEEK_SET);
  i64 result = vfs_read((i64)fd, (void *)buf, count);
  vfs_seek((i64)fd, saved, SEEK_SET);
  return (u64)result;
}

u64 sys_pwrite64(u64 fd, u64 buf, u64 count, u64 offset, u64 a5, u64 a6)
{
  (void)a5;
  (void)a6;

  if(!user_buf_ok(buf, count))
    return (u64)-EFAULT;
  if(fd <= 2)
    return sys_write(fd, buf, count, 0, 0, 0);

  i64 saved = vfs_seek((i64)fd, 0, SEEK_CUR);
  vfs_seek((i64)fd, (i64)offset, SEEK_SET);
  i64 result = vfs_write((i64)fd, (const void *)buf, count);
  vfs_seek((i64)fd, saved, SEEK_SET);
  return (u64)result;
}

u64 sys_symlink(u64 target, u64 linkpath, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)target;
  (void)linkpath;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)-ENOSYS;
}

u64 sys_openat(u64 dirfd, u64 path, u64 flags, u64 mode, u64 a5, u64 a6)
{
  const i64 AT_FDCWD = -100;
  if((i64)dirfd != AT_FDCWD)
    return (u64)-ENOSYS;
  return sys_open(path, flags, mode, 0, a5, a6);
}

u64 sys_readlink(u64 path, u64 buf, u64 bufsiz, u64 a4, u64 a5, u64 a6)
{
  (void)path;
  (void)buf;
  (void)bufsiz;
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)-ENOSYS;
}
