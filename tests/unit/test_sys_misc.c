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

/* vmm_get_phys: map uaddr → fake PA = uaddr | 0x1000 (non-zero, 4-aligned). */
static bool g_phys_ok = true;
u64         vmm_get_phys(u64 v)
{
  if(!g_phys_ok || !v || (v & 3ULL))
    return 0;
  return (v & ~0xFFFULL) | 0x1000ULL; /* fake but non-zero, aligned */
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
      cmocka_unit_test_setup(prlimit64_old_limit_filled, setup),
      cmocka_unit_test_setup(prlimit64_null_old_limit_noop, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
