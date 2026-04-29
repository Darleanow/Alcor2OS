/**
 * @file src/kernel/sys/pipe.c
 * @brief Anonymous pipes backed by a fixed-size ring buffer.
 *
 * Pipes live in the open file table as VFS_FD_PIPE_READ / VFS_FD_PIPE_WRITE
 * entries; per-process file descriptors point at those entries the same way
 * file fds do. End-of-pipe lifetime is reference-counted via the OFT
 * refcount, so fork-inheritance and dup/dup2 work the same as for files.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/mm/vmm.h>

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     16

typedef struct pipe
{
  u8  buffer[PIPE_BUF_SIZE];
  u64 read_pos;
  u64 write_pos;
  u64 count;
  int read_open;
  int write_open;
  int allocated;
} pipe_t;

static pipe_t pipes[MAX_PIPES];

static pipe_t *alloc_pipe(void)
{
  for(int i = 0; i < MAX_PIPES; i++) {
    if(!pipes[i].allocated) {
      kzero(&pipes[i], sizeof(pipes[i]));
      pipes[i].allocated  = 1;
      pipes[i].read_open  = 1;
      pipes[i].write_open = 1;
      return &pipes[i];
    }
  }
  return NULL;
}

void *pipe_alloc_obj(void)
{
  return alloc_pipe();
}

void pipe_oft_release(i32 kind, void *pipe_ptr)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p)
    return;

  if(kind == VFS_FD_PIPE_READ)
    p->read_open = 0;
  else if(kind == VFS_FD_PIPE_WRITE)
    p->write_open = 0;

  if(!p->read_open && !p->write_open)
    p->allocated = 0;
}

i64 pipe_read_obj(void *pipe_ptr, void *buf, u64 count)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p || !p->allocated)
    return -EBADF;
  if(!p->read_open)
    return -EBADF;

  if(p->count == 0 && !p->write_open)
    return 0;

  while(p->count == 0 && p->write_open)
    __asm__ volatile("pause");

  if(p->count == 0)
    return 0; /* write end closed mid-wait */

  u64 to_read = count > p->count ? p->count : count;
  u8 *dst     = (u8 *)buf;
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;
  return (i64)to_read;
}

i64 pipe_write_obj(void *pipe_ptr, const void *buf, u64 count)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p || !p->allocated)
    return -EBADF;
  if(!p->write_open)
    return -EBADF;
  if(!p->read_open)
    return -EPIPE;

  while(p->count >= PIPE_BUF_SIZE && p->read_open)
    __asm__ volatile("pause");

  u64       space    = PIPE_BUF_SIZE - p->count;
  u64       to_write = count > space ? space : count;
  const u8 *src      = (const u8 *)buf;

  for(u64 i = 0; i < to_write; i++) {
    p->buffer[p->write_pos] = src[i];
    p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count += to_write;
  return (i64)to_write;
}

u64 sys_pipe(u64 pipefd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!pipefd)
    return (u64)-EFAULT;
  if(!vmm_is_user_range((void *)pipefd, sizeof(int) * 2))
    return (u64)-EFAULT;
  if(!proc_current())
    return (u64)-EINVAL;

  pipe_t *p = alloc_pipe();
  if(!p)
    return (u64)-ENOMEM;

  i32 read_oft = vfs_oft_alloc_pipe(VFS_FD_PIPE_READ, p);
  if(read_oft < 0) {
    p->allocated = 0;
    return (u64)-ENFILE;
  }
  i32 write_oft = vfs_oft_alloc_pipe(VFS_FD_PIPE_WRITE, p);
  if(write_oft < 0) {
    vfs_oft_release(read_oft);
    return (u64)-ENFILE;
  }

  i64 read_fd = vfs_install_fd(read_oft);
  if(read_fd < 0) {
    vfs_oft_release(read_oft);
    vfs_oft_release(write_oft);
    return (u64)read_fd;
  }
  i64 write_fd = vfs_install_fd(write_oft);
  if(write_fd < 0) {
    vfs_oft_release(write_oft);
    proc_current()->fds[read_fd] = -1;
    vfs_oft_release(read_oft);
    return (u64)write_fd;
  }

  int *fds = (int *)pipefd;
  fds[0]   = (int)read_fd;
  fds[1]   = (int)write_fd;
  return 0;
}
