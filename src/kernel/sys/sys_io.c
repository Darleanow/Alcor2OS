/**
 * @file src/kernel/sys/sys_io.c
 * @brief I/O syscalls: `read`, `readv`, `write`, `lseek`, `ioctl`, `nanosleep`,
 * `writev`, `select`, `poll`.
 *
 * FD 0: keyboard (wait for IRQ with STI/HLT). Other FDs: VFS or pipes depending
 * on descriptor.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kbd.h>
#include <alcor2/kstdlib.h>
#include <alcor2/ktermios.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

static inline bool user_rw_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

/* Stdout fallback for fd 1/2 when the per-process fd table has no OFT entry
 * mapped — keeps the console wired up for unredirected programs. (Stdin uses
 * kbd_read_translated directly via the layout-aware path.) */
static u64 stdout_fallback(u64 buf, u64 count)
{
  const char *str = (const char *)buf;
  for(u64 i = 0; i < count; i++)
    console_putchar(str[i]);
  return count;
}

static bool fd_has_oft(u64 fd)
{
  proc_t *p = proc_current();
  return p && fd < (u64)VFS_MAX_FD && p->fds[fd] >= 0;
}

u64 sys_read(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_rw_ok(buf, count))
    return (u64)-EFAULT;
  if(count == 0)
    return 0;

  if(fd == 0 && !fd_has_oft(fd)) {
    proc_t *p = proc_current();
    if(p)
      return kbd_read_for_process(p, (char *)buf, count);
    return kbd_read_translated((char *)buf, count);
  }

  return (u64)vfs_read((i64)fd, (void *)buf, count);
}

u64 sys_write(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_rw_ok(buf, count))
    return (u64)-EFAULT;
  if(count == 0)
    return 0;

  if((fd == 1 || fd == 2) && !fd_has_oft(fd))
    return stdout_fallback(buf, count);

  return (u64)vfs_write((i64)fd, (void *)buf, count);
}

u64 sys_lseek(u64 fd, u64 offset, u64 whence, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)vfs_seek((i64)fd, (i64)offset, (i32)whence);
}

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403 /* TCSADRAIN */
#define TCSETSF    0x5404 /* TCSAFLUSH — used by ncurses raw()/noraw() */
#define TIOCGWINSZ 0x5413

static u64 ioctl_tty_emulated(proc_t *p, u64 request, u64 arg)
{
  switch(request) {
  case TIOCGWINSZ: {
    if(!user_rw_ok(arg, 8))
      return (u64)-EFAULT;
    struct
    {
      u16 row, col, xpixel, ypixel;
    } ws = {25, 80, 0, 0};
    (void)ws.row;
    (void)ws.col;
    (void)ws.xpixel;
    (void)ws.ypixel;
    kmemcpy((void *)arg, &ws, sizeof(ws));
    return 0;
  }
  case TCGETS:
    if(!p)
      return (u64)-EINVAL;
    if(!user_rw_ok(arg, sizeof(k_termios_t)))
      return (u64)-EFAULT;
    kmemcpy((void *)arg, &p->termios, sizeof(p->termios));
    return 0;
  case TCSETS:
  case TCSETSW:
  case TCSETSF:
    if(!p)
      return (u64)-EINVAL;
    if(!user_rw_ok(arg, sizeof(k_termios_t)))
      return (u64)-EFAULT;
    kmemcpy(&p->termios, (void *)arg, sizeof(p->termios));
    return 0;
  default:
    return (u64)-ENOTTY;
  }
}

u64 sys_ioctl(u64 fd, u64 request, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd == 0 && request == ALCOR2_IOC_KBD_SET_LAYOUT) {
    u32 lid;
    if(!user_rw_ok(arg, sizeof(lid)))
      return (u64)-EFAULT;
    kmemcpy(&lid, (void *)arg, sizeof(lid));
    if(lid >= KBD_LAYOUT_COUNT)
      return (u64)-EINVAL;
    kbd_set_layout((kbd_layout_t)lid);
    return 0;
  }

  /* stdio + pipe ends: isatty(3), ncurses tcgetattr/tcsetattr on stdout pipe.
   */
  if(fd <= 2 || vfs_fd_is_pipe(fd))
    return ioctl_tty_emulated(proc_current(), request, arg);

  return (u64)-ENOTTY;
}

u64 sys_nanosleep(u64 req, u64 rem, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)rem;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!req)
    return (u64)-EFAULT;

  struct
  {
    i64 sec;
    i64 nsec;
  }  *ts = (void *)req;

  u64 ms    = (u64)ts->sec * 1000 + (u64)ts->nsec / 1000000;
  u64 ticks = (ms + 9) / 10;
  if(ticks == 0)
    ticks = 1;

  for(u64 i = 0; i < ticks; i++) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
    cpu_disable_interrupts();
  }
  return 0;
}

struct iovec
{
  void *iov_base;
  u64   iov_len;
};

#define SYS_READV_IOV_MAX 1024

u64 sys_readv(u64 fd, u64 iov_ptr, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(iovcnt == 0 || iovcnt > SYS_READV_IOV_MAX)
    return (u64)-EINVAL;
  if(!iov_ptr || !user_rw_ok(iov_ptr, iovcnt * sizeof(struct iovec)))
    return (u64)-EFAULT;

  const struct iovec *vec   = (const struct iovec *)iov_ptr;
  u64                 total = 0;

  for(u64 i = 0; i < iovcnt; i++) {
    if(!vec[i].iov_base || vec[i].iov_len == 0)
      continue;
    if(!user_rw_ok((u64)vec[i].iov_base, vec[i].iov_len))
      return (u64)-EFAULT;

    u64 chunk = sys_read(fd, (u64)vec[i].iov_base, vec[i].iov_len, 0, 0, 0);
    if((i64)chunk < 0)
      return chunk;
    total += chunk;
    if(chunk < vec[i].iov_len)
      break;
  }

  return total;
}

u64 sys_writev(u64 fd, u64 iov, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!iov)
    return (u64)-EFAULT;

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

static inline bool sel_fdisset(const unsigned long *s, u32 fd)
{
  if(fd >= SEL_FDSET_LONG * SEL_NFDBITS)
    return false;
  return (s[fd / SEL_NFDBITS] & (1UL << (fd % SEL_NFDBITS))) != 0;
}

static inline void sel_fdclr(unsigned long *s, u32 fd)
{
  if(fd < SEL_FDSET_LONG * SEL_NFDBITS)
    s[fd / SEL_NFDBITS] &= ~(1UL << (fd % SEL_NFDBITS));
}

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

static i32 sel_read_ready(u64 fd)
{
  if(fd >= VFS_MAX_FD)
    return -EBADF;
  if(fd == 0 && !fd_has_oft(fd)) {
    proc_t *p = proc_current();
    if(!p)
      return kbd_raw_pending() ? 1 : 0;
    return kbd_select_read_ready(p) ? 1 : 0;
  }
  if((fd == 1 || fd == 2) && !fd_has_oft(fd))
    return -EBADF;
  return vfs_select_read_ready((i64)fd);
}

static i32 sel_write_ready(u64 fd)
{
  if(fd >= VFS_MAX_FD)
    return -EBADF;
  if((fd == 1 || fd == 2) && !fd_has_oft(fd))
    return 1;
  if(fd == 0 && !fd_has_oft(fd))
    return -EBADF;
  return vfs_select_write_ready((i64)fd);
}

/** @brief ~10 ms of wall time per tick (matches @c sys_nanosleep heuristics). */
static u64 io__ms_to_hlt_ticks(u64 ms)
{
  u64 t = (ms + 9) / 10;
  return t ? t : 1;
}

/** Linux-compatible @c poll(2) event bits (subset). */
#define POLL__IN   0x001
#define POLL__PRI  0x002
#define POLL__OUT  0x004
#define POLL__NVAL 0x020

#define POLL__MAX_NFDS VFS_MAX_FD

typedef struct
{
  i32 fd;
  i16 events;
  i16 revents;
} poll__fd_abi_t;

static void poll__timeout_from_ms(
    i32 timeout_ms, bool *immediate, bool *infinite, u64 *wait_ticks
)
{
  if(timeout_ms < 0) {
    *immediate  = false;
    *infinite   = true;
    *wait_ticks = 0;
    return;
  }
  *infinite = false;
  if(timeout_ms == 0) {
    *immediate  = true;
    *wait_ticks = 0;
    return;
  }
  *immediate = false;
  u64 ms    = (u64)timeout_ms;
  *wait_ticks = io__ms_to_hlt_ticks(ms);
}

static bool poll__fd_is_open(i32 fd)
{
  if(fd < 0)
    return false;
  if(fd == 0 && !fd_has_oft(0))
    return true;
  if((fd == 1 || fd == 2) && !fd_has_oft((u64)fd))
    return true;
  return vfs_fd_is_valid(fd);
}

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

  if(tv.sec == 0 && tv.nsec_usec == 0) {
    *poll_immediate = true;
    *wait_ticks     = 0;
    return 0;
  }

  u64 ms = (u64)tv.sec * 1000 + (u64)tv.nsec_usec / 1000;
  if(tv.nsec_usec && ms == 0)
    ms = 1;
  *poll_immediate = false;
  *wait_ticks     = io__ms_to_hlt_ticks(ms);
  return 0;
}

static void sel_hlt_slice(void)
{
  cpu_enable_interrupts();
  __asm__ volatile("hlt");
  cpu_disable_interrupts();
}

u64 sys_select(
    u64 nfds_u, u64 readfds, u64 writefds, u64 exceptfds, u64 timeout, u64 a6
)
{
  (void)a6;

  if(nfds_u > 1024)
    return (u64)-EINVAL;

  u32  nfds      = (u32)nfds_u;
  u32  nlongs    = nfds ? (nfds + (SEL_NFDBITS - 1)) / SEL_NFDBITS : 0;
  bool poll_mode = false;
  u64  ticks_rem = 0;
  bool infinite  = false;

  if(nfds && !readfds && !writefds && !exceptfds)
    return (u64)-EINVAL;

  if(nfds == 0) {
    if(timeout) {
      i32 prc = parse_timeval(timeout, &poll_mode, &ticks_rem);
      if(prc)
        return (u64)prc;
      if(poll_mode)
        return 0;
      for(u64 t = 0; t < ticks_rem; t++)
        sel_hlt_slice();
      return 0;
    }
    for(;;)
      sel_hlt_slice();
  }

  if(nlongs > SEL_FDSET_LONG)
    return (u64)-EINVAL;

  unsigned long rin[SEL_FDSET_LONG], win[SEL_FDSET_LONG], ein[SEL_FDSET_LONG];
  unsigned long rout[SEL_FDSET_LONG], wout[SEL_FDSET_LONG],
      eout[SEL_FDSET_LONG];

  kzero(rin, sizeof(rin));
  kzero(win, sizeof(win));
  kzero(ein, sizeof(ein));

  if(readfds) {
    if(!user_rw_ok(readfds, (u64)nlongs * sizeof(unsigned long)))
      return (u64)-EFAULT;
    kmemcpy(rin, (void *)readfds, (u64)nlongs * sizeof(unsigned long));
  }
  if(writefds) {
    if(!user_rw_ok(writefds, (u64)nlongs * sizeof(unsigned long)))
      return (u64)-EFAULT;
    kmemcpy(win, (void *)writefds, (u64)nlongs * sizeof(unsigned long));
  }
  if(exceptfds) {
    if(!user_rw_ok(exceptfds, (u64)nlongs * sizeof(unsigned long)))
      return (u64)-EFAULT;
    kmemcpy(ein, (void *)exceptfds, (u64)nlongs * sizeof(unsigned long));
  }

  sel_mask_high_bits(rin, win, ein, nfds, nlongs);

  if(timeout) {
    i32 prc = parse_timeval(timeout, &poll_mode, &ticks_rem);
    if(prc)
      return (u64)prc;
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
      return (u64)total;
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

u64 sys_poll(u64 fds, u64 nfds_u, u64 timeout_u, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(nfds_u > (u64)POLL__MAX_NFDS)
    return (u64)-EINVAL;

  u32 nfds = (u32)nfds_u;
  if(nfds != 0 && (!fds || !user_rw_ok(fds, (u64)nfds * sizeof(poll__fd_abi_t))))
    return (u64)-EFAULT;

  i32       timeout_ms = (i32)timeout_u;
  bool      immediate, infinite;
  u64       ticks_rem = 0;
  poll__fd_abi_t local[POLL__MAX_NFDS];

  poll__timeout_from_ms(timeout_ms, &immediate, &infinite, &ticks_rem);

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
