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

/**
 * @brief Block until @p p has data or the write end closes.
 *
 * Parks @c proc_current as the single reader, yields via @ref
 * proc_schedule, then on wake-up checks for a pending unmasked signal
 * (proc_signal wakes BLOCKED → READY) so the syscall return path runs
 * the handler / default action instead of looping back into block.
 *
 * @param p  Pipe to wait on.
 * @return 0 once data is available or the write end closed, @c -EINTR on
 *         signal.
 */
static i64 pipe_wait_for_data(pipe_t *p)
{
  while(p->count == 0 && p->write_open) {
    proc_t *me = proc_current();
    if(me) {
      p->waiting_reader = me;
      proc_block(me);
    }
    proc_schedule();
    if(me)
      p->waiting_reader = NULL;
    if(proc_signal_pending(me))
      return -EINTR;
  }
  return 0;
}

/**
 * @brief Block until @p p has free space or the read end closes.
 *
 * Mirror of @ref pipe_wait_for_data on the writer side.
 *
 * @param p  Pipe to wait on.
 * @return 0 once space is available or the read end closed, @c -EINTR on
 *         signal.
 */
static i64 pipe_wait_for_space(pipe_t *p)
{
  while(p->count >= PIPE_BUF_SIZE && p->read_open) {
    proc_t *me = proc_current();
    if(me) {
      p->waiting_writer = me;
      proc_block(me);
    }
    proc_schedule();
    if(me)
      p->waiting_writer = NULL;
    if(proc_signal_pending(me))
      return -EINTR;
  }
  return 0;
}

/**
 * @brief Copy up to @p to_read bytes out of the pipe's ring into @p dst.
 *
 * Companion to @ref pipe_copy_in — same single-source-of-truth rationale
 * for the wrap-around math, just in the opposite direction.
 *
 * @param p        Pipe with at least @p to_read queued bytes.
 * @param dst      Destination buffer.
 * @param to_read  Bytes to copy (caller computed against @c p->count).
 * @return Bytes copied.
 */
static u64 pipe_copy_out(pipe_t *p, u8 *dst, u64 to_read)
{
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;
  return to_read;
}

/**
 * @brief Read up to @p count bytes from pipe @p pipe_ptr.
 *
 * Blocks until data arrives or both ends close. Partial-on-signal: returns
 * @c -EINTR if no data was read before the signal, otherwise the bytes
 * already copied.
 *
 * @param pipe_ptr  Opaque @c pipe_t pointer (VFS contract).
 * @param buf       Destination buffer.
 * @param count     Maximum bytes to read.
 * @return Bytes read, 0 at EOF, or negative errno.
 */
i64 pipe_read_obj(void *pipe_ptr, void *buf, u64 count)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p || !p->allocated || !p->read_open)
    return -EBADF;
  if(p->count == 0 && !p->write_open)
    return 0;

  i64 wret = pipe_wait_for_data(p);
  if(wret < 0)
    return wret;
  if(p->count == 0)
    return 0; /* write end closed mid-wait */

  u64 to_read = count > p->count ? p->count : count;
  pipe_copy_out(p, (u8 *)buf, to_read);

  if(p->waiting_writer) {
    proc_wake(p->waiting_writer);
    p->waiting_writer = NULL;
  }
  return (i64)to_read;
}

/**
 * @brief Copy up to @p space bytes from @p src into the pipe's ring.
 *
 * Pulled out so @ref pipe_write_obj's outer loop reads as wait → copy →
 * wake, without inlining the ring-pointer arithmetic.
 *
 * @param p      Pipe with at least one free byte.
 * @param src    Source bytes (@p src_offset already applied by caller).
 * @param space  Bytes free in the ring (caller computed).
 * @return Bytes copied.
 */
static u64 pipe_copy_in(pipe_t *p, const u8 *src, u64 space)
{
  for(u64 i = 0; i < space; i++) {
    p->buffer[p->write_pos] = src[i];
    p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count += space;
  return space;
}

/**
 * @brief Run one iteration of the @ref pipe_write_obj loop.
 *
 * Wait for space, abort if the read end closed mid-wait, copy as much as
 * fits in the ring, then wake any blocked reader. Pulled out so the
 * outer loop reads as a simple progress accumulator.
 *
 * @param p          Pipe being written to.
 * @param src        Source bytes for this chunk.
 * @param remaining  Bytes still pending in the caller's write.
 * @return Bytes written on success, @c -EPIPE / @c -EINTR on failure.
 */
static i64 pipe_write_chunk(pipe_t *p, const u8 *src, u64 remaining)
{
  i64 wret = pipe_wait_for_space(p);
  if(wret < 0)
    return wret;
  if(!p->read_open)
    return -EPIPE;

  u64 space    = PIPE_BUF_SIZE - p->count;
  u64 to_write = remaining > space ? space : remaining;
  pipe_copy_in(p, src, to_write);

  if(p->waiting_reader) {
    proc_wake(p->waiting_reader);
    p->waiting_reader = NULL;
  }
  return (i64)to_write;
}

/**
 * @brief Write up to @p count bytes into pipe @p pipe_ptr.
 *
 * Blocks per-chunk until space is available; returns the bytes already
 * written on signal so the caller surfaces partial-write semantics.
 *
 * @param pipe_ptr  Opaque @c pipe_t pointer.
 * @param buf       Source buffer.
 * @param count     Bytes to write.
 * @return Bytes written, or negative errno on a hard error.
 */
i64 pipe_write_obj(void *pipe_ptr, const void *buf, u64 count)
{
  pipe_t *p = (pipe_t *)pipe_ptr;
  if(!p || !p->allocated || !p->write_open)
    return -EBADF;
  if(!p->read_open)
    return -EPIPE;

  const u8 *src     = (const u8 *)buf;
  u64       written = 0;
  while(written < count) {
    i64 r = pipe_write_chunk(p, src + written, count - written);
    if(r < 0)
      return written > 0 ? (i64)written : r;
    written += (u64)r;
  }
  return (i64)written;
}
