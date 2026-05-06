/**
 * @file src/kernel/sys/sys_misc.c
 * @brief Misc syscalls: `uname`, time, minimal `futex`, `sched_yield`.
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

/* Blocking futex. Keys map to the CPU physical backing of each 4-byte futex
 * word — safe with CLONE_THREAD (shared PTEs) unlike raw user VAs alone. */

#define FUTEX_WAIT              0
#define FUTEX_WAKE              1
#define FUTEX_FD                2
#define FUTEX_REQUEUE           3
#define FUTEX_CMP_REQUEUE       4
#define FUTEX_WAKE_OP           5
#define FUTEX_LOCK_PI           6
#define FUTEX_UNLOCK_PI         7
#define FUTEX_TRYLOCK_PI        8
#define FUTEX_WAIT_BITSET       9
#define FUTEX_WAKE_BITSET       10
#define FUTEX_WAIT_REQUEUE_PI   11
#define FUTEX_CMP_REQUEUE_PI    12
#define FUTEX_PRIVATE_FLAG      128
#define FUTEX_CLOCK_REALTIME    256

#define FUTEX_QUEUE_LEN 256

extern void proc_schedule(void);

static struct
{
  u64     key_pa; /**< Full physical addr of backing u32 word */
  proc_t *waiter;
} g_futex_q[FUTEX_QUEUE_LEN];

static u64 futex_key_pa(u64 uaddr)
{
  if(!uaddr || (uaddr & 3ULL) != 0)
    return 0;
  u64 pa = vmm_get_phys(uaddr);
  return pa;
}

static bool futex_read_u32(u64 uaddr, u32 *out)
{
  if(!user_buf_ok(uaddr, sizeof(u32)))
    return false;
  u64 pa = vmm_get_phys(uaddr);
  if(!pa)
    return false;
  const volatile u32 *kv =
      (const volatile u32 *)((u8 *)phys_to_virt(pa) + 0); /* NOLINT */
  *out = *kv;
  return true;
}

static u64 futex_wake_pa(u64 key_pa, u64 max_wake)
{
  u64 woke = 0;
  if(!key_pa || max_wake == 0)
    return 0;

  for(int i = 0; i < FUTEX_QUEUE_LEN && woke < max_wake; i++) {
    if(g_futex_q[i].key_pa != key_pa || !g_futex_q[i].waiter)
      continue;

    proc_t *w           = g_futex_q[i].waiter;
    g_futex_q[i].key_pa = 0;
    g_futex_q[i].waiter = NULL;
    if(w && w->state == PROC_STATE_BLOCKED)
      w->state = PROC_STATE_READY;
    woke++;
  }

  return woke;
}

static u64 futex_requeue_pa(u64 from_pa, u64 to_pa, u64 max_mv)
{
  u64 mv = 0;
  if(!from_pa || !to_pa || from_pa == to_pa || max_mv == 0)
    return 0;

  for(int i = 0; i < FUTEX_QUEUE_LEN && mv < max_mv; i++) {
    if(g_futex_q[i].key_pa != from_pa || !g_futex_q[i].waiter)
      continue;
    g_futex_q[i].key_pa = to_pa;
    mv++;
  }
  return mv;
}

u64 sys_futex(u64 uaddr, u64 op, u64 val, u64 timeout, u64 uaddr2, u64 val3)
{
  (void)timeout;

  u32 cmd = (u32)op & ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);

  /* Priority-inheritance / lock-pi: not implemented in full; treat as the
   * closest non-PI primitive so libstdc++/musl does not spin into abort. */
  if(cmd == FUTEX_LOCK_PI || cmd == FUTEX_TRYLOCK_PI || cmd == FUTEX_UNLOCK_PI)
    return 0;
  if(cmd == FUTEX_WAIT_REQUEUE_PI)
    cmd = FUTEX_WAIT;
  if(cmd == FUTEX_CMP_REQUEUE_PI)
    cmd = FUTEX_CMP_REQUEUE;

  if(cmd == FUTEX_WAKE_OP) {
    /* Full WAKE_OP decode is huge; lld/musl only need forward progress. */
    u64 k = futex_key_pa(uaddr);
    if(!k)
      return (u64)-EFAULT;
    return futex_wake_pa(k, 256);
  }

  if(cmd == FUTEX_FD)
    return (u64)-ENOSYS;

  if(cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
    u32 curv;
    if(!futex_read_u32(uaddr, &curv))
      return (u64)-EFAULT;
    if(curv != (u32)val)
      return (u64)-EAGAIN;

    u64 key = futex_key_pa(uaddr);
    if(!key)
      return (u64)-EFAULT;

    proc_t *cur = proc_current();
    if(!cur)
      return (u64)-ESRCH;

    int slot = -1;
    for(int i = 0; i < FUTEX_QUEUE_LEN; i++) {
      if(g_futex_q[i].key_pa == 0) {
        slot = i;
        break;
      }
    }
    if(slot < 0)
      return (u64)-ENOMEM;

    g_futex_q[slot].key_pa = key;
    g_futex_q[slot].waiter = cur;
    cur->state             = PROC_STATE_BLOCKED;
    proc_schedule();

    if(g_futex_q[slot].waiter == cur) {
      g_futex_q[slot].key_pa = 0;
      g_futex_q[slot].waiter = NULL;
    }
    return 0;
  }

  if(cmd == FUTEX_WAKE || cmd == FUTEX_WAKE_BITSET) {
    u64 k = futex_key_pa(uaddr);
    if(!k)
      return (u64)-EFAULT;
    return futex_wake_pa(k, val ? val : ~(u64)0);
  }

  /* REQUEUE variants (musl pthread_cond). Keep semantics loose: wake every
   * waiter on @p uaddr, then requeue every remaining waiter onto @p uaddr2.
   * (Linux passes wake/requeue counts in r10/r9 — we ignore them so we never
   * leave waiters stuck if those args are mis-parsed.) */
  if(cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE) {
    (void)val3;

    if(cmd == FUTEX_CMP_REQUEUE) {
      u32 curv;
      if(!futex_read_u32(uaddr, &curv))
        return (u64)-EFAULT;
      if(curv != (u32)val)
        return (u64)-EAGAIN;
    }

    if(!user_buf_ok(uaddr2, sizeof(u32)))
      return (u64)-EFAULT;

    u64 k1 = futex_key_pa(uaddr);
    u64 k2 = futex_key_pa(uaddr2);
    if(!k1 || !k2)
      return (u64)-EFAULT;

    u64 wk = futex_wake_pa(k1, ~(u64)0);
    u64 rq = futex_requeue_pa(k1, k2, ~(u64)0);
    (void)timeout;
    return wk + rq;
  }

  return (u64)-ENOSYS;
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

/**
 * @brief Stub for sched_getaffinity(pid, cpusetsize, mask).
 *
 * We have a single-CPU kernel so the affinity mask is always {0}. Writes
 * one byte (bit 0 set) into the user-supplied @p mask buffer and returns
 * the size of the populated mask in bytes (Linux convention).
 */
u64 sys_sched_getaffinity(u64 pid, u64 cpusetsize, u64 mask, u64 a4, u64 a5,
                          u64 a6)
{
  (void)pid;
  (void)a4;
  (void)a5;
  (void)a6;

  if(cpusetsize == 0 || !mask)
    return (u64)-EINVAL;

  if(!vmm_is_user_range((void *)mask, cpusetsize))
    return (u64)-EFAULT;

  u8 *m = (u8 *)mask;
  m[0]  = 0x01; /* CPU 0 only */
  for(u64 i = 1; i < cpusetsize; i++)
    m[i] = 0;
  return cpusetsize;
}

/** Linux rlimit struct: each value is rlim_t (u64 on x86_64). */
struct k_rlimit
{
  u64 rlim_cur;
  u64 rlim_max;
};

/* RLIMIT_* indices we care about. */
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_AS      9

/** Fill in a generous default rlimit so userland (clang/musl) doesn't bail. */
static void fill_default_rlimit(u64 resource, struct k_rlimit *out)
{
  out->rlim_cur = (u64)-1; /* RLIM_INFINITY */
  out->rlim_max = (u64)-1;

  switch(resource) {
  case RLIMIT_STACK:
    out->rlim_cur = 8ULL * 1024 * 1024;       /* 8 MiB */
    out->rlim_max = 64ULL * 1024 * 1024;      /* 64 MiB */
    break;
  case RLIMIT_NOFILE:
    out->rlim_cur = 1024;
    out->rlim_max = 4096;
    break;
  case RLIMIT_NPROC:
    out->rlim_cur = 256;
    out->rlim_max = 256;
    break;
  default:
    break;
  }
}

u64 sys_getrlimit(u64 resource, u64 rlim, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_buf_ok(rlim, sizeof(struct k_rlimit)))
    return (u64)-EFAULT;

  fill_default_rlimit(resource, (struct k_rlimit *)rlim);
  return 0;
}

u64 sys_prlimit64(u64 pid, u64 resource, u64 new_limit, u64 old_limit,
                  u64 a5, u64 a6)
{
  (void)pid;
  (void)new_limit; /* setting limits is a no-op */
  (void)a5;
  (void)a6;

  if(old_limit) {
    if(!user_buf_ok(old_limit, sizeof(struct k_rlimit)))
      return (u64)-EFAULT;
    fill_default_rlimit(resource, (struct k_rlimit *)old_limit);
  }
  return 0;
}
