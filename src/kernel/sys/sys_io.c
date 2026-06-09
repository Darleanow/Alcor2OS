/**
 * @file src/kernel/sys/sys_io.c
 * @brief I/O syscalls: read, readv, write, writev, lseek, ioctl, nanosleep,
 *        select, poll.
 *
 * Every fd is dispatched through the VFS layer. Stdio (fd 0/1/2) gets its
 * behaviour from the @c /dev/tty chardev installed by @c proc_start_first;
 * the underlying tty_ops in @c dev_nodes.c handles the kbd/fb_console wiring.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/pit.h>
#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

/** @brief Return @c true if @p ptr..@p ptr+size is a valid user read/write
 * range. */
static inline bool user_rw_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

/**
 * @brief Read up to @p count bytes from @p fd into @p buf.
 *
 * @param fd     File descriptor to read from.
 * @param buf    User-space destination buffer (must be writable for @p count
 *               bytes).
 * @param count  Maximum bytes to read.
 * @return Bytes read on success (0 on EOF), negative @c -errno on failure
 *         (@c -EFAULT, @c -EBADF, etc.).
 */
kern_err_t sys_read(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_rw_ok(buf, count))
    return -EFAULT;
  if(count == 0)
    return 0;

  return (u64)vfs_read((i64)fd, (void *)buf, count);
}

/**
 * @brief Write @p count bytes from @p buf to @p fd.
 *
 * @param fd     File descriptor to write to.
 * @param buf    User-space source buffer (must be readable for @p count
 *               bytes).
 * @param count  Bytes to write.
 * @return Bytes written on success, negative @c -errno on failure.
 */
kern_err_t sys_write(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_rw_ok(buf, count))
    return -EFAULT;
  if(count == 0)
    return 0;

  return (u64)vfs_write((i64)fd, (void *)buf, count);
}

/** @brief Reposition the file offset of @p fd (@c lseek). */
kern_err_t sys_lseek(u64 fd, u64 offset, u64 whence, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)vfs_seek((i64)fd, (i64)offset, (i32)whence);
}

/**
 * @brief Perform a device control operation on @p fd.
 *
 * Every request is routed through the VFS: tty_ops in @c dev_nodes.c handles
 * the stdio termios + ALCOR2/FB_CONSOLE ioctls, mouse_ops handles
 * @c /dev/mouse, and pipes / regular files return @c -ENOTTY.
 */
kern_err_t sys_ioctl(u64 fd, u64 request, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)vfs_ioctl((i64)fd, request, arg);
}

/**
 * @brief Sleep for the duration described by @p req (@c struct @c timespec).
 *
 * Implemented as a sequence of STI/HLT pairs, each ~10 ms.  @p rem is not
 * filled because preemption is cooperative and we don't track elapsed time.
 */
kern_err_t sys_nanosleep(u64 req, u64 rem, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)rem;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  struct timespec
  {
    i64 sec;
    i64 nsec;
  };

  if(!user_rw_ok(req, sizeof(struct timespec)))
    return -EFAULT;

  const struct timespec *ts = (const struct timespec *)req;
  if(ts->sec < 0 || ts->nsec < 0 || ts->nsec >= 1000000000)
    return -EINVAL;

  /* Sleep against the monotonic ns clock so the duration holds regardless of
   * the PIT rate. HLT wakes on each tick; the loop re-checks until the deadline
   * (so the actual sleep is at least one tick). */
  u64 want_ns  = (u64)ts->sec * 1000000000ULL + (u64)ts->nsec;
  u64 deadline = pit_get_ns() + want_ns;
  while(pit_get_ns() < deadline) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
    cpu_disable_interrupts();
  }
  return 0;
}

/** @brief Scatter-gather read: read from @p fd into @p iovcnt buffers. */
struct iovec
{
  void *iov_base;
  u64   iov_len;
};

#define SYS_READV_IOV_MAX 1024

kern_err_t sys_readv(u64 fd, u64 iov_ptr, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(iovcnt == 0 || iovcnt > SYS_READV_IOV_MAX)
    return -EINVAL;
  if(!iov_ptr || !user_rw_ok(iov_ptr, iovcnt * sizeof(struct iovec)))
    return -EFAULT;

  const struct iovec *vec   = (const struct iovec *)iov_ptr;
  u64                 total = 0;

  for(u64 i = 0; i < iovcnt; i++) {
    if(!vec[i].iov_base || vec[i].iov_len == 0)
      continue;
    if(!user_rw_ok((u64)vec[i].iov_base, vec[i].iov_len))
      return -EFAULT;

    u64 chunk = sys_read(fd, (u64)vec[i].iov_base, vec[i].iov_len, 0, 0, 0);
    if((i64)chunk < 0)
      return chunk;
    total += chunk;
    if(chunk < vec[i].iov_len)
      break;
  }

  return total;
}

/**
 * @brief Gathered-write across @p iovcnt iovecs into @p fd.
 *
 * Each iov is dispatched as a separate ::sys_write - the VFS driver decides
 * how to coalesce. @c tty_write goes through @c fb_console_write whose
 * begin/end pair is idempotent enough that a multi-iov refresh stays cheap.
 *
 * @param fd      File descriptor to write to.
 * @param iov     User-space pointer to @c struct @c iovec array.
 * @param iovcnt  Number of iovecs in the array.
 * @return Total bytes written on success, negative @c -errno on failure.
 */
kern_err_t sys_writev(u64 fd, u64 iov, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!iov)
    return -EFAULT;

  const struct iovec *vec   = (const struct iovec *)iov;
  u64                 total = 0;
  for(u64 i = 0; i < iovcnt; i++) {
    if(vec[i].iov_base && vec[i].iov_len > 0) {
      u64 written =
          sys_write(fd, (u64)vec[i].iov_base, vec[i].iov_len, 0, 0, 0);
      if((i64)written < 0)
        return written;
      total += written;
    }
  }
  return total;
}

#define SEL_NFDBITS    64
#define SEL_FDSET_LONG 16
#define SEL_FDSET_SZ   (SEL_FDSET_LONG * sizeof(unsigned long))

/** @brief Test whether bit @p fd is set in the @c fd_set @p s. */
static inline bool sel_fdisset(const unsigned long *s, u32 fd)
{
  if(fd >= SEL_FDSET_LONG * SEL_NFDBITS)
    return false;
  return (s[fd / SEL_NFDBITS] & (1UL << (fd % SEL_NFDBITS))) != 0;
}

/** @brief Clear bit @p fd in the @c fd_set @p s. */
static inline void sel_fdclr(unsigned long *s, u32 fd)
{
  if(fd < SEL_FDSET_LONG * SEL_NFDBITS)
    s[fd / SEL_NFDBITS] &= ~(1UL << (fd % SEL_NFDBITS));
}

/** @brief Zero bits above @p nfds in the three @c fd_set arrays to avoid stale
 * results. */
static inline void sel_mask_high_bits(
    unsigned long *r, unsigned long *w, unsigned long *e, u32 nfds, u32 nlongs
)
{
  if(nfds % SEL_NFDBITS) {
    unsigned long m = (1UL << (nfds % SEL_NFDBITS)) - 1UL;
    u32           i = nlongs - 1;
    r[i] &= m;
    w[i] &= m;
    e[i] &= m;
  }
}

/** @brief Return positive if @p fd has data available, 0 if not, negative on
 * error. */
static i32 sel_read_ready(u64 fd)
{
  if(fd >= VFS_MAX_FD)
    return -EBADF;
  return vfs_select_read_ready((i64)fd);
}

/** @brief Return positive if @p fd can accept a write, 0 if not, negative on
 * error. */
static i32 sel_write_ready(u64 fd)
{
  if(fd >= VFS_MAX_FD)
    return -EBADF;
  return vfs_select_write_ready((i64)fd);
}

/** @brief Convert a millisecond timeout to whole HLT ticks at the current PIT
 * rate, rounding up to at least one. */
static u64 io__ms_to_hlt_ticks(u64 ms)
{
  u64 hz      = pit_get_frequency();
  u64 ms_tick = 1000u / hz;
  if(ms_tick == 0)
    ms_tick = 1;
  u64 t = (ms + ms_tick - 1) / ms_tick;
  return t ? t : 1;
}

/** POSIX @c poll(2) event bits (subset). */
#define POLL__IN       0x001
#define POLL__PRI      0x002
#define POLL__OUT      0x004
#define POLL__NVAL     0x020
#define POLL__MAX_NFDS VFS_MAX_FD

typedef struct
{
  i32 fd;
  i16 events;
  i16 revents;
} poll__fd_abi_t;

/**
 * @brief Compute select/poll timeout parameters from a millisecond value.
 *
 * Negative @p ms_signed means infinite wait.  Zero means poll-and-return.
 * Positive values are converted to HLT ticks via ::io__ms_to_hlt_ticks.
 */
static void io__timeout_calc(
    i32 ms_signed, bool *immediate, bool *infinite, u64 *wait_ticks
)
{
  if(ms_signed < 0) {
    *immediate  = false;
    *infinite   = true;
    *wait_ticks = 0;
    return;
  }
  *infinite = false;
  if(ms_signed == 0) {
    *immediate  = true;
    *wait_ticks = 0;
    return;
  }
  *immediate  = false;
  u64 ms      = (u64)ms_signed;
  *wait_ticks = io__ms_to_hlt_ticks(ms);
}

/** @brief Return @c true if @p fd is open (including unmapped stdio fds). */
static bool poll__fd_is_open(i32 fd)
{
  if(fd < 0)
    return false;
  return vfs_fd_is_valid(fd);
}

/** @brief Fill @p e->revents for one poll entry; return non-zero if ready. */
static int poll__fill_one(poll__fd_abi_t *e)
{
  e->revents = 0;
  if(e->fd < 0)
    return 0;
  if(!poll__fd_is_open(e->fd)) {
    e->revents = POLL__NVAL;
    return 1;
  }

  i16 want = e->events;
  if(want & (POLL__IN | POLL__PRI)) {
    i32 st = sel_read_ready((u64)e->fd);
    if(st > 0) {
      if(want & POLL__IN)
        e->revents |= POLL__IN;
      if(want & POLL__PRI)
        e->revents |= POLL__PRI;
    }
  }
  if(want & POLL__OUT) {
    i32 st = sel_write_ready((u64)e->fd);
    if(st > 0)
      e->revents |= POLL__OUT;
  }
  return e->revents != 0;
}

/**
 * @brief Scan @p nfds descriptors and fill @p rout / @p wout / @p eout.
 *
 * Copies @p rin / @p win into the output sets, then clears each bit that is
 * not ready.  @p eout is always zeroed (no exceptional condition support).
 * Returns negative errno on the first bad fd, otherwise 0 with @p *total set.
 */
static i32 select_scan(
    u32 nfds, const unsigned long *rin, const unsigned long *win,
    unsigned long *rout, unsigned long *wout, unsigned long *eout, int *total
)
{
  int n = 0;
  kmemcpy(rout, rin, SEL_FDSET_SZ);
  kmemcpy(wout, win, SEL_FDSET_SZ);
  kzero(eout, SEL_FDSET_SZ);

  for(u32 fd = 0; fd < nfds; fd++) {
    if(sel_fdisset(rin, fd)) {
      i32 st = sel_read_ready(fd);
      if(st < 0)
        return st;
      if(!st)
        sel_fdclr(rout, fd);
      else
        n++;
    }
    if(sel_fdisset(win, fd)) {
      i32 st = sel_write_ready(fd);
      if(st < 0)
        return st;
      if(!st)
        sel_fdclr(wout, fd);
      else
        n++;
    }
  }

  *total = n;
  return 0;
}

/**
 * @brief Parse a @c struct @c timeval from user space into poll parameters.
 *
 * @return 0 on success, negative errno if the pointer is bad or values out of
 * range.
 */
static i32 parse_timeval(u64 timeout_ptr, bool *poll_immediate, u64 *wait_ticks)
{
  struct
  {
    i64 sec;
    i64 nsec_usec;
  } tv;

  if(!user_rw_ok(timeout_ptr, sizeof(tv)))
    return -EFAULT;
  kmemcpy(&tv, (void *)timeout_ptr, sizeof(tv));
  if(tv.sec < 0 || tv.nsec_usec < 0 || tv.nsec_usec >= 1000000)
    return -EINVAL;

  u64  ms = (u64)tv.sec * 1000 + (u64)tv.nsec_usec / 1000;
  bool infinite;
  io__timeout_calc((i32)ms, poll_immediate, &infinite, wait_ticks);
  return 0;
}

/** @brief Yield the CPU for one ~10 ms timer tick, then yield to any other
 *         READY user proc.
 *
 * Syscall context runs with interrupts masked, so the timer alone doesn't
 * preempt this proc into another one (see "Pipe busy-wait must yield" in
 * user/shell/README.md). Without the explicit @c proc_schedule() call, a
 * select/poll loop here would hog the CPU for the full timeout, and any
 * child it's polling on (e.g. an ncurses app writing into the shell's
 * relay pipe) would never run.
 *
 * Matches the pattern used by the keyboard wait loop in @c kbd_layout.c. */
static void sel_hlt_slice(void)
{
  cpu_enable_interrupts();
  __asm__ volatile("hlt");
  cpu_disable_interrupts();
  proc_schedule();
}

/**
 * @brief Monitor up to @p nfds_u descriptors for I/O readiness (@c select).
 *
 * Copies the caller's @c fd_set bitmaps into kernel buffers, scans them in a
 * loop sleeping one HLT tick per iteration until at least one fd is ready or
 * the timeout expires.  The output sets are zeroed on timeout.
 */
kern_err_t sys_select(
    u64 nfds_u, u64 readfds, u64 writefds, u64 exceptfds, u64 timeout, u64 a6
)
{
  (void)a6;

  if(nfds_u > 1024)
    return -EINVAL;

  u32  nfds      = (u32)nfds_u;
  u32  nlongs    = nfds ? (nfds + (SEL_NFDBITS - 1)) / SEL_NFDBITS : 0;
  bool poll_mode = false;
  u64  ticks_rem = 0;
  bool infinite  = false;

  if(nfds && !readfds && !writefds && !exceptfds)
    return -EINVAL;

  if(nfds == 0) {
    if(timeout) {
      bool immediate = false;
      i32  prc       = parse_timeval(timeout, &immediate, &ticks_rem);
      if(prc)
        return (u64)prc;
      if(immediate)
        return 0;
      for(u64 t = 0; t < ticks_rem; t++)
        sel_hlt_slice();
      return 0;
    }
    for(;;)
      sel_hlt_slice();
  }

  if(nlongs > SEL_FDSET_LONG)
    return -EINVAL;

  unsigned long rin[SEL_FDSET_LONG], win[SEL_FDSET_LONG], ein[SEL_FDSET_LONG];
  unsigned long rout[SEL_FDSET_LONG], wout[SEL_FDSET_LONG],
      eout[SEL_FDSET_LONG];

  kzero(rin, sizeof(rin));
  kzero(win, sizeof(win));
  kzero(ein, sizeof(ein));

  if(readfds) {
    if(!user_rw_ok(readfds, (u64)nlongs * sizeof(unsigned long)))
      return -EFAULT;
    kmemcpy(rin, (void *)readfds, (u64)nlongs * sizeof(unsigned long));
  }
  if(writefds) {
    if(!user_rw_ok(writefds, (u64)nlongs * sizeof(unsigned long)))
      return -EFAULT;
    kmemcpy(win, (void *)writefds, (u64)nlongs * sizeof(unsigned long));
  }
  if(exceptfds) {
    if(!user_rw_ok(exceptfds, (u64)nlongs * sizeof(unsigned long)))
      return -EFAULT;
    kmemcpy(ein, (void *)exceptfds, (u64)nlongs * sizeof(unsigned long));
  }

  sel_mask_high_bits(rin, win, ein, nfds, nlongs);

  if(timeout) {
    bool immediate = false;
    i32  prc       = parse_timeval(timeout, &immediate, &ticks_rem);
    if(prc)
      return (u64)prc;
    poll_mode = immediate;
  } else
    infinite = true;

  for(;;) {
    int total = 0;
    i32 err   = select_scan(nfds, rin, win, rout, wout, eout, &total);
    if(err)
      return (u64)err;

    if(total > 0 || poll_mode) {
      if(readfds)
        kmemcpy((void *)readfds, rout, (u64)nlongs * sizeof(unsigned long));
      if(writefds)
        kmemcpy((void *)writefds, wout, (u64)nlongs * sizeof(unsigned long));
      if(exceptfds)
        kmemcpy((void *)exceptfds, eout, (u64)nlongs * sizeof(unsigned long));
      return total;
    }

    if(!infinite) {
      if(ticks_rem == 0)
        break;
      ticks_rem--;
    }

    sel_hlt_slice();
  }

  kzero(rout, sizeof(rout));
  kzero(wout, sizeof(wout));
  kzero(eout, sizeof(eout));
  if(readfds)
    kmemcpy((void *)readfds, rout, (u64)nlongs * sizeof(unsigned long));
  if(writefds)
    kmemcpy((void *)writefds, wout, (u64)nlongs * sizeof(unsigned long));
  if(exceptfds)
    kmemcpy((void *)exceptfds, eout, (u64)nlongs * sizeof(unsigned long));
  return 0;
}

/**
 * @brief Wait for events on an array of @p nfds_u file descriptors (@c poll).
 *
 * Copies the @c pollfd array into a kernel-side buffer, checks readiness in a
 * loop sleeping one HLT tick per iteration, and copies results back on exit.
 * Returns 0 on timeout, the number of ready fds otherwise.
 */
kern_err_t sys_poll(u64 fds, u64 nfds_u, u64 timeout_u, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(nfds_u > (u64)POLL__MAX_NFDS)
    return -EINVAL;

  u32 nfds = (u32)nfds_u;
  if(nfds != 0 &&
     (!fds || !user_rw_ok(fds, (u64)nfds * sizeof(poll__fd_abi_t))))
    return -EFAULT;

  i32            timeout_ms = (i32)timeout_u;
  bool           immediate, infinite;
  u64            ticks_rem = 0;
  poll__fd_abi_t local[POLL__MAX_NFDS];

  io__timeout_calc(timeout_ms, &immediate, &infinite, &ticks_rem);

  if(nfds == 0) {
    if(immediate)
      return 0;
    if(infinite) {
      for(;;)
        sel_hlt_slice();
    }
    for(u64 t = 0; t < ticks_rem; t++)
      sel_hlt_slice();
    return 0;
  }

  kmemcpy(local, (void *)fds, (u64)nfds * sizeof(poll__fd_abi_t));

  for(;;) {
    int nready = 0;
    for(u32 i = 0; i < nfds; i++) {
      if(poll__fill_one(&local[i]))
        nready++;
    }

    if(nready > 0 || immediate) {
      kmemcpy((void *)fds, local, (u64)nfds * sizeof(poll__fd_abi_t));
      return (u64)nready;
    }

    if(!infinite) {
      if(ticks_rem == 0)
        break;
      ticks_rem--;
    }
    sel_hlt_slice();
  }

  for(u32 i = 0; i < nfds; i++)
    local[i].revents = 0;
  kmemcpy((void *)fds, local, (u64)nfds * sizeof(poll__fd_abi_t));
  return 0;
}
