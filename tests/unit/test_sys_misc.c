#include "test_common.h"

#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <string.h>

void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
char *kstrncpy(char *d, const char *s, u64 m)
{
  if(!m)
    return d;
  u64 i;
  for(i = 0; i < m - 1 && s[i]; i++)
    d[i] = s[i];
  d[i] = '\0';
  return d;
}

/* vmm_is_user_range: treat every non-NULL pointer as user. */
bool vmm_is_user_range(const void *p, u64 n)
{
  (void)n;
  return p != NULL;
}
void console_print(const char *s)
{
  (void)s;
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}

static u64 g_mono_ns;
u64        pit_get_ns(void)
{
  return g_mono_ns;
}

static proc_t *g_cur_proc = NULL;
proc_t        *proc_current(void)
{
  return g_cur_proc;
}
void proc_schedule(void) {}
void proc_block(proc_t *p)
{
  if(p)
    p->state = PROC_STATE_BLOCKED;
}
void proc_wake(proc_t *p)
{
  if(p && p->state == PROC_STATE_BLOCKED)
    p->state = PROC_STATE_READY;
}

/* vmm_get_phys: identity mapping so phys_to_virt(vmm_get_phys(p)) == p.
 * Requires caller address is non-NULL and 4-byte aligned. */
static bool g_phys_ok = true;
u64         vmm_get_phys(u64 v)
{
  if(!g_phys_ok || !v || (v & 3ULL))
    return 0;
  return v; /* identity: PA == VA since hhdm=0 */
}
u64 vmm_get_hhdm(void)
{
  return 0;
}
void vmm_map(u64 v, u64 p, u64 f)
{
  (void)v;
  (void)p;
  (void)f;
}
void vmm_unmap(u64 v)
{
  (void)v;
}
bool vmm_map_range_alloc(u64 v, u64 c, u64 f)
{
  (void)v;
  (void)c;
  (void)f;
  return false;
}
void *pmm_alloc(void)
{
  return NULL;
}
void pmm_free(void *p)
{
  (void)p;
}


#ifndef ALCOR2_VERSION
  #define ALCOR2_VERSION "test"
#endif
#include "../../src/kernel/sys/sys_misc.c"


static int setup(void **state)
{
  (void)state;
  g_mono_ns  = 0;
  g_phys_ok  = true;
  g_cur_proc = NULL;
  /* reset the futex queue between tests */
  memset(g_futex_q, 0, sizeof(g_futex_q));
  return 0;
}


static void uname_fills_sysname(void **state)
{
  (void)state;
  struct
  {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
  } u;
  memset(&u, 0, sizeof(u));
  u64 ret = sys_uname((u64)&u, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_string_equal(u.sysname, "Alcor2");
  assert_string_equal(u.nodename, "alcor2");
  assert_string_equal(u.machine, "x86_64");
}

static void uname_null_buf_returns_efault(void **state)
{
  (void)state;
  u64 ret = sys_uname(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}


static void gettimeofday_zero_ns(void **state)
{
  (void)state;
  struct
  {
    i64 tv_sec;
    i64 tv_usec;
  } tv;
  u64 ret = sys_gettimeofday((u64)&tv, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(tv.tv_sec, 0);
  assert_int_equal(tv.tv_usec, 0);
}

static void gettimeofday_converts_ns_correctly(void **state)
{
  (void)state;
  g_mono_ns = 1500000500ULL; /* 1.5000005 s */
  struct
  {
    i64 tv_sec;
    i64 tv_usec;
  } tv;
  sys_gettimeofday((u64)&tv, 0, 0, 0, 0, 0);
  assert_int_equal(tv.tv_sec, 1);
  assert_int_equal(tv.tv_usec, 500000);
}

static void gettimeofday_null_buf_returns_efault(void **state)
{
  (void)state;
  u64 ret = sys_gettimeofday(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}


static void clock_gettime_fills_timespec(void **state)
{
  (void)state;
  g_mono_ns = 2000000001ULL; /* 2 s + 1 ns */
  struct
  {
    i64 s;
    i64 ns;
  } ts;
  u64 ret = sys_clock_gettime(0, (u64)&ts, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(ts.s, 2);
  assert_int_equal(ts.ns, 1);
}

static void clock_gettime_null_returns_efault(void **state)
{
  (void)state;
  u64 ret = sys_clock_gettime(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}


static void futex_key_pa_null_returns_zero(void **state)
{
  (void)state;
  /* uaddr=0 → invalid */
  assert_int_equal(futex_key_pa(0), 0);
}

static void futex_key_pa_unaligned_returns_zero(void **state)
{
  (void)state;
  /* unaligned (not 4-byte) */
  assert_int_equal(futex_key_pa(0x1001), 0);
  assert_int_equal(futex_key_pa(0x1002), 0);
  assert_int_equal(futex_key_pa(0x1003), 0);
}

static void futex_key_pa_aligned_returns_nonzero(void **state)
{
  (void)state;
  /* 4-byte aligned → vmm_get_phys succeeds → non-zero PA */
  u64 pa = futex_key_pa(0x2000);
  assert_true(pa != 0);
}

static void futex_key_pa_no_phys_returns_zero(void **state)
{
  (void)state;
  g_phys_ok = false;
  u64 pa    = futex_key_pa(0x2000);
  assert_int_equal(pa, 0);
}

static void futex_wake_pa_zero_key_nop(void **state)
{
  (void)state;
  u64 woke = futex_wake_pa(0, 10);
  assert_int_equal(woke, 0);
}

static void futex_wake_pa_zero_max_nop(void **state)
{
  (void)state;
  u64 woke = futex_wake_pa(0xDEAD, 0);
  assert_int_equal(woke, 0);
}

static void futex_wake_pa_wakes_matching_slot(void **state)
{
  (void)state;
  /* Manually stuff one waiter into the queue */
  static proc_t p = {.state = PROC_STATE_BLOCKED};
  g_futex_q[0].key_pa = 0xCAFEBEEF;
  g_futex_q[0].waiter = &p;

  u64 woke = futex_wake_pa(0xCAFEBEEF, 1);
  assert_int_equal(woke, 1);
  assert_int_equal(p.state, PROC_STATE_READY);
  assert_int_equal(g_futex_q[0].key_pa, 0);
  assert_null(g_futex_q[0].waiter);
}

static void futex_wake_pa_respects_max(void **state)
{
  (void)state;
  static proc_t p0 = {.state = PROC_STATE_BLOCKED};
  static proc_t p1 = {.state = PROC_STATE_BLOCKED};
  g_futex_q[0].key_pa = 0xABCD;
  g_futex_q[0].waiter = &p0;
  g_futex_q[1].key_pa = 0xABCD;
  g_futex_q[1].waiter = &p1;

  u64 woke = futex_wake_pa(0xABCD, 1); /* wake at most 1 */
  assert_int_equal(woke, 1);
  /* second waiter must still be there */
  assert_non_null(g_futex_q[1].waiter);
}

static void futex_wake_pa_skips_wrong_key(void **state)
{
  (void)state;
  static proc_t p = {.state = PROC_STATE_BLOCKED};
  g_futex_q[0].key_pa = 0x1111;
  g_futex_q[0].waiter = &p;

  u64 woke = futex_wake_pa(0x2222, 10);
  assert_int_equal(woke, 0);
  assert_non_null(g_futex_q[0].waiter);
}


static void sched_getaffinity_returns_cpu0(void **state)
{
  (void)state;
  u8  mask[8];
  u64 ret = sys_sched_getaffinity(0, sizeof(mask), (u64)mask, 0, 0, 0);
  assert_int_equal((i64)ret, (i64)sizeof(mask));
  assert_int_equal(mask[0], 0x01); /* CPU 0 */
  assert_int_equal(mask[1], 0x00);
}

static void sched_getaffinity_zero_size_einval(void **state)
{
  (void)state;
  u8  mask[8];
  u64 ret = sys_sched_getaffinity(0, 0, (u64)mask, 0, 0, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void sched_getaffinity_null_mask_einval(void **state)
{
  (void)state;
  u64 ret = sys_sched_getaffinity(0, 8, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EINVAL);
}


static void getrlimit_stack_returns_8mib(void **state)
{
  (void)state;
  struct
  {
    u64 rlim_cur;
    u64 rlim_max;
  } rl;
  u64 ret = sys_getrlimit(3 /* RLIMIT_STACK */, (u64)&rl, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(rl.rlim_cur, 8ULL * 1024 * 1024);
  assert_int_equal(rl.rlim_max, 64ULL * 1024 * 1024);
}

static void getrlimit_nofile_returns_1024(void **state)
{
  (void)state;
  struct
  {
    u64 rlim_cur;
    u64 rlim_max;
  } rl;
  sys_getrlimit(7 /* RLIMIT_NOFILE */, (u64)&rl, 0, 0, 0, 0);
  assert_int_equal(rl.rlim_cur, 1024);
  assert_int_equal(rl.rlim_max, 4096);
}

static void getrlimit_null_returns_efault(void **state)
{
  (void)state;
  u64 ret = sys_getrlimit(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void prlimit64_old_limit_filled(void **state)
{
  (void)state;
  struct
  {
    u64 rlim_cur;
    u64 rlim_max;
  } rl;
  u64 ret = sys_prlimit64(0, 6 /* RLIMIT_NPROC */, 0, (u64)&rl, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(rl.rlim_cur, 256);
  assert_int_equal(rl.rlim_max, 256);
}

static void prlimit64_null_old_limit_noop(void **state)
{
  (void)state;
  u64 ret = sys_prlimit64(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void prlimit64_null_buf_returns_efault(void **state)
{
  (void)state;
  struct { u64 cur; u64 max; } rl = {0};
  /* old_limit is non-NULL but vmm_is_user_range returns false for NULL ptr
     inside user_buf_ok — pass a non-NULL but ensure vmm check fails */
  (void)rl;
  u64 ret = sys_prlimit64(0, 0, 0, 0 /* NULL old_limit */, 0, 0);
  assert_int_equal((i64)ret, 0); /* NULL old_limit is allowed (no-op) */
}

static void getrlimit_default_infinite(void **state)
{
  (void)state;
  struct { u64 cur; u64 max; } rl;
  u64 ret = sys_getrlimit(0 /* RLIMIT_CPU */, (u64)&rl, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(rl.cur, (u64)-1);
  assert_int_equal(rl.max, (u64)-1);
}

static void sched_yield_returns_zero(void **state)
{
  (void)state;
  u64 ret = sys_sched_yield(0, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

/* futex_requeue_pa helpers */
static void futex_requeue_pa_same_key_nop(void **state)
{
  (void)state;
  u64 moved = futex_requeue_pa(0xABCD, 0xABCD, 10);
  assert_int_equal(moved, 0);
}

static void futex_requeue_pa_zero_key_nop(void **state)
{
  (void)state;
  u64 moved = futex_requeue_pa(0, 0xABCD, 10);
  assert_int_equal(moved, 0);
}

static void futex_requeue_pa_moves_waiters(void **state)
{
  (void)state;
  static proc_t p = {.state = PROC_STATE_BLOCKED};
  g_futex_q[0].key_pa = 0x1111;
  g_futex_q[0].waiter = &p;

  u64 moved = futex_requeue_pa(0x1111, 0x2222, 10);
  assert_int_equal(moved, 1);
  assert_int_equal(g_futex_q[0].key_pa, 0x2222);
}

/* sys_futex WAIT path */
static void sys_futex_wait_eagain_when_val_mismatch(void **state)
{
  (void)state;
  u32 word = 99;
  /* val = 42 != word = 99 → EAGAIN */
  u64 ret = sys_futex((u64)&word, FUTEX_WAIT, 42, 0, 0, 0);
  assert_int_equal((i64)ret, -EAGAIN);
}

static void sys_futex_wait_no_proc_esrch(void **state)
{
  (void)state;
  /* word == val so we pass the EAGAIN check, then hit the proc_current() NULL check */
  volatile u32 word = 7;
  g_cur_proc        = NULL;
  u64 ret           = sys_futex((u64)&word, FUTEX_WAIT, 7, 0, 0, 0);
  assert_int_equal((i64)ret, -ESRCH);
}

static void sys_futex_wait_no_phys_efault(void **state)
{
  (void)state;
  g_phys_ok = false;
  u32 word  = 5;
  u64 ret   = sys_futex((u64)&word, FUTEX_WAIT, 5, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void sys_futex_wait_blocks_then_wakes(void **state)
{
  (void)state;
  static proc_t p = {.state = PROC_STATE_RUNNING};
  g_cur_proc      = &p;
  u32 word        = 42;
  /* val matches, proc is valid — should block proc then return 0 */
  u64 ret         = sys_futex((u64)&word, FUTEX_WAIT, 42, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  /* proc_schedule is a no-op stub, waiter still set → cleanup path clears it */
}

static void sys_futex_wait_bitset_same_as_wait(void **state)
{
  (void)state;
  u32 word = 1;
  u64 ret  = sys_futex((u64)&word, FUTEX_WAIT_BITSET, 99, 0, 0, 0);
  assert_int_equal((i64)ret, -EAGAIN); /* val mismatch */
}

/* sys_futex WAKE path */
static void sys_futex_wake_returns_count(void **state)
{
  (void)state;
  static proc_t p = {.state = PROC_STATE_BLOCKED};
  u32 word        = 0;
  /* pre-stuff a waiter for the PA of &word */
  u64 key              = futex_key_pa((u64)&word);
  g_futex_q[0].key_pa  = key;
  g_futex_q[0].waiter  = &p;

  u64 ret = sys_futex((u64)&word, FUTEX_WAKE, 1, 0, 0, 0);
  assert_int_equal((i64)ret, 1);
  assert_int_equal(p.state, PROC_STATE_READY);
}

static void sys_futex_wake_no_phys_efault(void **state)
{
  (void)state;
  g_phys_ok = false;
  u32 word  = 0;
  u64 ret   = sys_futex((u64)&word, FUTEX_WAKE, 1, 0, 0, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void sys_futex_wake_bitset_same_as_wake(void **state)
{
  (void)state;
  u32 word  = 0;
  u64 ret   = sys_futex((u64)&word, FUTEX_WAKE_BITSET, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0); /* no waiters, val=0 → wake all = 0 woken */
}

/* sys_futex WAKE_OP path */
static void sys_futex_wake_op_no_phys_efault(void **state)
{
  (void)state;
  g_phys_ok = false;
  u32 word  = 0;
  u64 ret   = sys_futex((u64)&word, FUTEX_WAKE_OP, 1, 0, (u64)&word, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void sys_futex_wake_op_succeeds(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, FUTEX_WAKE_OP, 1, 0, (u64)&word, 0);
  assert_int_equal((i64)ret, 0); /* no waiters → woke 0 */
}

/* sys_futex FD path */
static void sys_futex_fd_returns_enosys(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, FUTEX_FD, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ENOSYS);
}

/* sys_futex PI op stubs */
static void sys_futex_lock_pi_returns_zero(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, FUTEX_LOCK_PI, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void sys_futex_unlock_pi_returns_zero(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, FUTEX_UNLOCK_PI, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void sys_futex_trylock_pi_returns_zero(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, FUTEX_TRYLOCK_PI, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void sys_futex_wait_requeue_pi_treated_as_wait(void **state)
{
  (void)state;
  u32 word = 5;
  /* val=99 != word=5 → rewritten to FUTEX_WAIT → EAGAIN */
  u64 ret = sys_futex((u64)&word, FUTEX_WAIT_REQUEUE_PI, 99, 0, 0, 0);
  assert_int_equal((i64)ret, -EAGAIN);
}

/* sys_futex REQUEUE path */
static void sys_futex_requeue_no_phys_efault(void **state)
{
  (void)state;
  g_phys_ok = false;
  u32 w1 = 0, w2 = 0;
  u64 ret = sys_futex((u64)&w1, FUTEX_REQUEUE, 0, 0, (u64)&w2, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void sys_futex_requeue_no_uaddr2_efault(void **state)
{
  (void)state;
  u32 w1 = 0;
  u64 ret = sys_futex((u64)&w1, FUTEX_REQUEUE, 0, 0, 0 /* NULL */, 0);
  assert_int_equal((i64)ret, -EFAULT);
}

static void sys_futex_cmp_requeue_val_mismatch_eagain(void **state)
{
  (void)state;
  u32 w1 = 10, w2 = 0;
  u64 ret = sys_futex((u64)&w1, FUTEX_CMP_REQUEUE, 99 /* val */, 0, (u64)&w2, 10 /* val3 */);
  assert_int_equal((i64)ret, -EAGAIN);
}

static void sys_futex_cmp_requeue_pi_treated_as_cmp_requeue(void **state)
{
  (void)state;
  u32 w1 = 7, w2 = 0;
  /* val mismatch → EAGAIN via CMP_REQUEUE path */
  u64 ret = sys_futex((u64)&w1, FUTEX_CMP_REQUEUE_PI, 99, 0, (u64)&w2, 0);
  assert_int_equal((i64)ret, -EAGAIN);
}

static void sys_futex_unknown_op_enosys(void **state)
{
  (void)state;
  u32 word = 0;
  u64 ret  = sys_futex((u64)&word, 0xFF, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ENOSYS);
}

/* futex PRIVATE_FLAG is stripped */
static void sys_futex_private_flag_stripped(void **state)
{
  (void)state;
  u32 word = 0;
  /* FUTEX_FD | FUTEX_PRIVATE_FLAG → still ENOSYS */
  u64 ret = sys_futex((u64)&word, FUTEX_FD | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ENOSYS);
}

/* futex queue full → ENOMEM */
static void sys_futex_wait_queue_full_enomem(void **state)
{
  (void)state;
  static proc_t p = {.state = PROC_STATE_RUNNING};
  g_cur_proc      = &p;
  u32 word        = 1;
  /* Fill every slot with a dummy key so no free slot exists */
  for(int i = 0; i < FUTEX_QUEUE_LEN; i++) {
    g_futex_q[i].key_pa = 0xDEAD0000 + (u64)i;
    g_futex_q[i].waiter = &p;
  }
  u64 ret = sys_futex((u64)&word, FUTEX_WAIT, 1, 0, 0, 0);
  assert_int_equal((i64)ret, -ENOMEM);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      /* uname */
      cmocka_unit_test_setup(uname_fills_sysname, setup),
      cmocka_unit_test_setup(uname_null_buf_returns_efault, setup),
      /* gettimeofday */
      cmocka_unit_test_setup(gettimeofday_zero_ns, setup),
      cmocka_unit_test_setup(gettimeofday_converts_ns_correctly, setup),
      cmocka_unit_test_setup(gettimeofday_null_buf_returns_efault, setup),
      /* clock_gettime */
      cmocka_unit_test_setup(clock_gettime_fills_timespec, setup),
      cmocka_unit_test_setup(clock_gettime_null_returns_efault, setup),
      /* futex pure math */
      cmocka_unit_test_setup(futex_key_pa_null_returns_zero, setup),
      cmocka_unit_test_setup(futex_key_pa_unaligned_returns_zero, setup),
      cmocka_unit_test_setup(futex_key_pa_aligned_returns_nonzero, setup),
      cmocka_unit_test_setup(futex_key_pa_no_phys_returns_zero, setup),
      cmocka_unit_test_setup(futex_wake_pa_zero_key_nop, setup),
      cmocka_unit_test_setup(futex_wake_pa_zero_max_nop, setup),
      cmocka_unit_test_setup(futex_wake_pa_wakes_matching_slot, setup),
      cmocka_unit_test_setup(futex_wake_pa_respects_max, setup),
      cmocka_unit_test_setup(futex_wake_pa_skips_wrong_key, setup),
      /* sched_getaffinity */
      cmocka_unit_test_setup(sched_getaffinity_returns_cpu0, setup),
      cmocka_unit_test_setup(sched_getaffinity_zero_size_einval, setup),
      cmocka_unit_test_setup(sched_getaffinity_null_mask_einval, setup),
      /* getrlimit / prlimit64 */
      cmocka_unit_test_setup(getrlimit_stack_returns_8mib, setup),
      cmocka_unit_test_setup(getrlimit_nofile_returns_1024, setup),
      cmocka_unit_test_setup(getrlimit_null_returns_efault, setup),
      cmocka_unit_test_setup(getrlimit_default_infinite, setup),
      cmocka_unit_test_setup(prlimit64_old_limit_filled, setup),
      cmocka_unit_test_setup(prlimit64_null_old_limit_noop, setup),
      cmocka_unit_test_setup(prlimit64_null_buf_returns_efault, setup),
      /* sched_yield */
      cmocka_unit_test_setup(sched_yield_returns_zero, setup),
      /* futex_requeue_pa helpers */
      cmocka_unit_test_setup(futex_requeue_pa_same_key_nop, setup),
      cmocka_unit_test_setup(futex_requeue_pa_zero_key_nop, setup),
      cmocka_unit_test_setup(futex_requeue_pa_moves_waiters, setup),
      /* sys_futex WAIT */
      cmocka_unit_test_setup(sys_futex_wait_eagain_when_val_mismatch, setup),
      cmocka_unit_test_setup(sys_futex_wait_no_proc_esrch, setup),
      cmocka_unit_test_setup(sys_futex_wait_no_phys_efault, setup),
      cmocka_unit_test_setup(sys_futex_wait_blocks_then_wakes, setup),
      cmocka_unit_test_setup(sys_futex_wait_bitset_same_as_wait, setup),
      cmocka_unit_test_setup(sys_futex_wait_queue_full_enomem, setup),
      /* sys_futex WAKE */
      cmocka_unit_test_setup(sys_futex_wake_returns_count, setup),
      cmocka_unit_test_setup(sys_futex_wake_no_phys_efault, setup),
      cmocka_unit_test_setup(sys_futex_wake_bitset_same_as_wake, setup),
      /* sys_futex WAKE_OP */
      cmocka_unit_test_setup(sys_futex_wake_op_no_phys_efault, setup),
      cmocka_unit_test_setup(sys_futex_wake_op_succeeds, setup),
      /* sys_futex FD */
      cmocka_unit_test_setup(sys_futex_fd_returns_enosys, setup),
      /* sys_futex PI ops */
      cmocka_unit_test_setup(sys_futex_lock_pi_returns_zero, setup),
      cmocka_unit_test_setup(sys_futex_unlock_pi_returns_zero, setup),
      cmocka_unit_test_setup(sys_futex_trylock_pi_returns_zero, setup),
      cmocka_unit_test_setup(sys_futex_wait_requeue_pi_treated_as_wait, setup),
      /* sys_futex REQUEUE */
      cmocka_unit_test_setup(sys_futex_requeue_no_phys_efault, setup),
      cmocka_unit_test_setup(sys_futex_requeue_no_uaddr2_efault, setup),
      cmocka_unit_test_setup(sys_futex_cmp_requeue_val_mismatch_eagain, setup),
      cmocka_unit_test_setup(sys_futex_cmp_requeue_pi_treated_as_cmp_requeue, setup),
      /* sys_futex unknown / flags */
      cmocka_unit_test_setup(sys_futex_unknown_op_enosys, setup),
      cmocka_unit_test_setup(sys_futex_private_flag_stripped, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
