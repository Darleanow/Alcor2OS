/**
 * @file src/kernel/sys/sys_misc.c
 * @brief Miscellaneous syscall implementations.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/mm/vmm.h>

static inline bool user_buf_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

u64 sys_uname(u64 buf, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  struct utsname
  {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
  } *u = (void *)buf;
  if(!user_buf_ok(buf, sizeof(*u)))
    return (u64)-EFAULT;

  kzero(u, sizeof(*u));
  kstrncpy(u->sysname, "Alcor2", 65);
  kstrncpy(u->nodename, "alcor2", 65);
  kstrncpy(u->release, "0.1.0", 65);
  kstrncpy(u->version, "Alcor2 OS", 65);
  kstrncpy(u->machine, "x86_64", 65);
  return 0;
}

u64 sys_gettimeofday(u64 tv, u64 tz, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)tz;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(tv, 16))
    return (u64)-EFAULT;

  struct
  {
    i64 tv_sec;
    i64 tv_usec;
  } *t = (void *)tv;

  t->tv_sec  = 0;
  t->tv_usec = 0;
  return 0;
}

u64 sys_futex(u64 uaddr, u64 op, u64 val, u64 timeout, u64 uaddr2, u64 val3)
{
  (void)uaddr;
  (void)op;
  (void)val;
  (void)timeout;
  (void)uaddr2;
  (void)val3;
  return 0;
}

u64 sys_clock_gettime(u64 clk, u64 tp, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)clk;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  struct
  {
    i64 s;
    i64 ns;
  } *ts = (void *)tp;
  if(!user_buf_ok(tp, sizeof(*ts)))
    return (u64)-EFAULT;
  ts->s = 0;
  ts->ns = 0;
  return 0;
}

u64 sys_sched_yield(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  /* User scheduling is proc_t-based; yield through proc scheduler. */
  proc_schedule();
  return 0;
}
