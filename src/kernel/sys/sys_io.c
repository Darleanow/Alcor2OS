/**
 * @file src/kernel/sys/sys_io.c
 * @brief I/O-oriented syscall implementations.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/arch/cpu.h>
#include <alcor2/errno.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/sys/internal.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/mm/vmm.h>

static inline bool user_rw_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
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

  if(fd == 0) {
    char *user_buf = (char *)buf;
    while(!keyboard_has_data()) {
      cpu_enable_interrupts();
      __asm__ volatile("hlt");
      cpu_disable_interrupts();
    }
    return keyboard_read(user_buf, count);
  }

  i64 pipe_result = pipe_read((int)fd, (void *)buf, count);
  if(pipe_result != -ENOENT)
    return (u64)pipe_result;

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

  if(fd == 1 || fd == 2) {
    const char *str = (const char *)buf;
    for(u64 i = 0; i < count; i++)
      console_putchar(str[i]);
    return count;
  }

  i64 pipe_result = pipe_write((int)fd, (const void *)buf, count);
  if(pipe_result != -ENOENT)
    return (u64)pipe_result;

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

  if(fd <= 2) {
    switch(request) {
    case TIOCGWINSZ: {
      struct
      {
        u16 row, col, x, y;
      } *ws = (void *)arg;
      if(ws) {
        ws->row = 25;
        ws->col = 80;
        ws->x   = 0;
        ws->y   = 0;
      }
      return 0;
    }
    case TCGETS:
    case TCSETS:
      return 0;
    default:
      break;
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
