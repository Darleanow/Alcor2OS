#include "test_common.h"

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

struct proc;
struct proc *proc_current(void)
{
  return NULL;
}
void proc_schedule(void) {}
void proc_block(struct proc *p)
{
  (void)p;
}
u64 vmm_get_phys(u64 v)
{
  (void)v;
  return 0;
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
void proc_wake(struct proc *p)
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
  g_mono_ns = 0;
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

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(uname_fills_sysname, setup),
      cmocka_unit_test_setup(uname_null_buf_returns_efault, setup),
      cmocka_unit_test_setup(gettimeofday_zero_ns, setup),
      cmocka_unit_test_setup(gettimeofday_converts_ns_correctly, setup),
      cmocka_unit_test_setup(gettimeofday_null_buf_returns_efault, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
