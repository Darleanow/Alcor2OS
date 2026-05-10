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
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

extern void proc_schedule(void);

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     16

typedef struct pipe
{
  u8      buffer[PIPE_BUF_SIZE];
  u64     read_pos;
  u64     write_pos;
  u64     count;
  int     read_open;
  int     write_open;
  int     allocated;
  proc_t *waiting_reader; /**< Process blocked waiting for data to read. */
  proc_t *waiting_writer; /**< Process blocked waiting for space to write. */
} pipe_t;

static pipe_t  pipes[MAX_PIPES];

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

void pipe_oft_retain(i32 kind, void *pipe_ptr)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p || !p->allocated)
    return;
  if(kind == VFS_FD_PIPE_READ)
    p->read_open++;
  else if(kind == VFS_FD_PIPE_WRITE)
    p->write_open++;
}

bool pipe_poll_read_ready(const void *pipe_ptr)
{
  const pipe_t *p = (const pipe_t *)pipe_ptr;
  if(!p || !p->allocated || !p->read_open)
    return false;
  if(p->count > 0)
    return true;
  return !p->write_open;
}

bool pipe_poll_write_ready(const void *pipe_ptr)
{
  const pipe_t *p = (const pipe_t *)pipe_ptr;
  if(!p || !p->allocated || !p->write_open)
    return false;
  if(!p->read_open)
    return true;
  return p->count < PIPE_BUF_SIZE;
}

void pipe_oft_release(i32 kind, void *pipe_ptr)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p)
    return;

  if(kind == VFS_FD_PIPE_READ) {
    if(p->read_open > 0)
      p->read_open--;
    /* Wake blocked writer so it sees EPIPE. */
    if(!p->read_open && p->waiting_writer &&
       p->waiting_writer->state == PROC_STATE_BLOCKED)
      p->waiting_writer->state = PROC_STATE_READY;
  } else if(kind == VFS_FD_PIPE_WRITE) {
    if(p->write_open > 0)
      p->write_open--;
    /* Wake blocked reader so it returns EOF (0). */
    if(!p->write_open && p->waiting_reader &&
       p->waiting_reader->state == PROC_STATE_BLOCKED)
      p->waiting_reader->state = PROC_STATE_READY;
  }

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

  /* Block (not spin) until data arrives or the write end closes. */
  while(p->count == 0 && p->write_open) {
    proc_t *me = proc_current();
    if(me) {
      p->waiting_reader = me;
      me->state         = PROC_STATE_BLOCKED;
    }
    proc_schedule();
    if(me)
      p->waiting_reader = NULL;
  }

  if(p->count == 0)
    return 0; /* write end closed mid-wait */

  u64 to_read = count > p->count ? p->count : count;
  u8 *dst     = (u8 *)buf;
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;

  /* Wake a blocked writer now that space is available. */
  if(p->waiting_writer && p->waiting_writer->state == PROC_STATE_BLOCKED)
    p->waiting_writer->state = PROC_STATE_READY;

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

  const u8 *src     = (const u8 *)buf;
  u64       written = 0;

  while(written < count) {
    /* Block (not spin) until space is available or the read end closes. */
    while(p->count >= PIPE_BUF_SIZE && p->read_open) {
      proc_t *me = proc_current();
      if(me) {
        p->waiting_writer = me;
        me->state         = PROC_STATE_BLOCKED;
      }
      proc_schedule();
      if(me)
        p->waiting_writer = NULL;
    }

    if(!p->read_open)
      return written > 0 ? (i64)written : -EPIPE;

    u64 space    = PIPE_BUF_SIZE - p->count;
    u64 to_write = count - written;
    if(to_write > space)
      to_write = space;

    for(u64 i = 0; i < to_write; i++) {
      p->buffer[p->write_pos] = src[written + i];
      p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += to_write;
    written += to_write;

    /* Wake a blocked reader now that data is available. */
    if(p->waiting_reader && p->waiting_reader->state == PROC_STATE_BLOCKED)
      p->waiting_reader->state = PROC_STATE_READY;
  }

  return (i64)written;
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

u64 sys_pipe2(u64 pipefd, u64 flags, u64 a3, u64 a4, u64 a5, u64 a6)
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
   * attribute — it must NOT be stored in the shared OFT entry. */
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
