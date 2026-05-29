/**
 * @file src/kernel/sys/pipe.c
 * @brief Anonymous pipes backed by a fixed-size ring buffer.
 *
 * Pipes live in the open file table as VFS_KIND_PIPE_RD / VFS_KIND_PIPE_WR
 * entries; per-process file descriptors point at those entries the same way
 * file fds do. End-of-pipe lifetime is reference-counted via the OFT
 * refcount, so fork-inheritance and dup/dup2 work the same as for files.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>

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

static pipe_t pipes[MAX_PIPES];

/** @brief Find a free pipe slot, zero it, and mark both ends open. */
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

void pipe_rd_release(void *pipe_ptr)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p)
    return;

  if(p->read_open > 0)
    p->read_open--;
  /* Wake blocked writer so it sees EPIPE. */
  if(!p->read_open && p->waiting_writer)
    proc_wake(p->waiting_writer);

  if(!p->read_open && !p->write_open)
    p->allocated = 0;
}

void pipe_wr_release(void *pipe_ptr)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p)
    return;

  if(p->write_open > 0)
    p->write_open--;
  /* Wake blocked reader so it returns EOF (0). */
  if(!p->write_open && p->waiting_reader) {
    proc_wake(p->waiting_reader);
    p->waiting_reader = NULL;
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
      proc_block(me);
    }
    proc_schedule();
    if(me)
      p->waiting_reader = NULL;
    /* A pending signal wakes the proc via proc_signal (BLOCKED → READY);
     * surface it as -EINTR so the syscall return path runs handlers /
     * default actions instead of looping back into proc_block. */
    if(proc_signal_pending(me))
      return -EINTR;
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
  if(p->waiting_writer) {
    proc_wake(p->waiting_writer);
    p->waiting_writer = NULL;
  }

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
        proc_block(me);
      }
      proc_schedule();
      if(me)
        p->waiting_writer = NULL;
      /* Partial-write semantics: return what we have so far on signal;
       * caller's next syscall return drives signal delivery. */
      if(proc_signal_pending(me))
        return written > 0 ? (i64)written : -EINTR;
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
    if(p->waiting_reader) {
      proc_wake(p->waiting_reader);
      p->waiting_reader = NULL;
    }
  }

  return (i64)written;
}
