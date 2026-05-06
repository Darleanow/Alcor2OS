/**
 * @file src/kernel/sys/sys_io.c
 * @brief I/O syscalls: `read`, `readv`, `write`, `lseek`, `ioctl`, `nanosleep`,
 * `writev`.
 *
 * FD 0: keyboard (wait for IRQ with STI/HLT). Other FDs: VFS or pipes depending on descriptor.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/arch/cpu.h>
#include <alcor2/errno.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/kbd.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/mm/vmm.h>

/* musl: isatty(3) probes TIOCGWINSZ; tcgetattr uses TCGETS (x86_64 termios is 60 bytes). */
#define MUSL_NCCS         32
#define MUSL_TERMIOS_SIZE 60

typedef struct {
  u32 c_iflag;
  u32 c_oflag;
  u32 c_cflag;
  u32 c_lflag;
  u8  c_line;
  u8  pad[3];
  u8  c_cc[MUSL_NCCS];
  u32 __c_ispeed;
  u32 __c_ospeed;
} musl_termios_t;

_Static_assert(sizeof(musl_termios_t) == MUSL_TERMIOS_SIZE, "musl_termios mismatch");

/* Values from musl arch/generic/bits/termios.h (octal literals). */

#define TG_ICRNL  0000400u
#define TG_ONLCR  0000004u
#define TG_CS8    0000060u
#define TG_CREAD  0000200u
#define TG_CLOCAL 0004000u
#define TG_ISIG   0000001u
#define TG_ICANON 0000002u
#define TG_ECHO   0000010u
#define TG_IEXTEN 0100000u
#define TG_B38400 0000017u

static void tty_fill_default_termios(musl_termios_t *t)
{
  kzero(t, sizeof(*t));
  t->c_iflag     = TG_ICRNL;
  t->c_oflag     = TG_ONLCR;
  t->c_cflag     = TG_CS8 | TG_CREAD | TG_CLOCAL;
  t->c_lflag     = TG_ISIG | TG_ICANON | TG_ECHO | TG_IEXTEN;
  t->__c_ispeed  = TG_B38400;
  t->__c_ospeed  = TG_B38400;
  t->c_cc[0]     = '\x03'; /* VINTR Ctrl+C */
  t->c_cc[1]     = 0x1c; /* VQUIT */
  t->c_cc[2]     = 0x7f; /* VERASE */
  t->c_cc[3]     = 0x15; /* VKILL */
  t->c_cc[4]     = '\x04'; /* VEOF */
}

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

  if(fd == 0 && !fd_has_oft(fd))
    return kbd_read_translated((char *)buf, count);

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
#define TIOCGWINSZ 0x5413

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

  if(fd <= 2) {
    switch(request) {
    case TIOCGWINSZ: {
      if(!user_rw_ok(arg, 8))
        return (u64)-EFAULT;
      struct
      {
        u16 row, col, xpixel, ypixel;
      } ws = {25, 80, 0, 0};
      kmemcpy((void *)arg, &ws, sizeof(ws));
      return 0;
    }
    case TCGETS:
      if(!user_rw_ok(arg, sizeof(musl_termios_t)))
        return (u64)-EFAULT;
      {
        musl_termios_t t;
        tty_fill_default_termios(&t);
        kmemcpy((void *)arg, &t, sizeof(t));
      }
      return 0;
    case TCSETS:
      if(!user_rw_ok(arg, sizeof(musl_termios_t)))
        return (u64)-EFAULT;
      return 0;
    default:
      return (u64)-ENOTTY;
    }
  }

  return 0;
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
  } *ts = (void *)req;

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

  const struct iovec *vec = (const struct iovec *)iov_ptr;
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
      u64 written = sys_write(fd, (u64)vec[i].iov_base, vec[i].iov_len, 0, 0, 0);
      if((i64)written < 0)
        return written;
      total += written;
    }
  }
  return total;
}
