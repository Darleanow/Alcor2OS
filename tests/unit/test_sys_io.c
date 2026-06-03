#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <string.h>

void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n)                  { memset(d, 0, n); }
void  console_print(const char *s)            { (void)s; }
void  console_printf(const char *fmt, ...)    { (void)fmt; }

/* vmm stubs */
static bool g_user_range_ok = true;
bool vmm_is_user_range(const void *p, u64 n)  { (void)p; (void)n; return g_user_range_ok; }
bool vmm_is_user_ptr(const void *p)            { (void)p; return g_user_range_ok; }

/* vfs stubs */
static i64 g_vfs_read_ret  = 5;
static i64 g_vfs_write_ret = 5;
static i64 g_vfs_seek_ret  = 0;
static i64 g_vfs_ioctl_ret = 0;
static i64 g_vfs_sel_read  = 1;
static i64 g_vfs_sel_write = 1;
i64 vfs_read(i64 fd, void *buf, u64 n)           { (void)fd; (void)buf; (void)n; return g_vfs_read_ret; }
i64 vfs_write(i64 fd, const void *buf, u64 n)    { (void)fd; (void)buf; (void)n; return g_vfs_write_ret; }
i64 vfs_seek(i64 fd, i64 off, i32 w)             { (void)fd; (void)off; (void)w; return g_vfs_seek_ret; }
i64 vfs_ioctl(i64 fd, u64 req, u64 arg)          { (void)fd; (void)req; (void)arg; return g_vfs_ioctl_ret; }
i32 vfs_select_read_ready(i64 fd)                { (void)fd; return (i32)g_vfs_sel_read; }
i32 vfs_select_write_ready(i64 fd)               { (void)fd; return (i32)g_vfs_sel_write; }
bool vfs_fd_is_valid(i64 fd)                     { (void)fd; return true; }

/* proc stubs */
static proc_t g_proc;
proc_t *proc_current(void) { return &g_proc; }
void proc_schedule(void) {}
void proc_block(proc_t *p) { (void)p; }
void proc_wake(proc_t *p)  { (void)p; }

/* cpu stubs */
void cpu_enable_interrupts(void)  {}
void cpu_disable_interrupts(void) {}

/* pit stubs */
static u64 g_pit_ns   = 0;
static u32 g_pit_freq = 100;
u64 pit_get_ns(void)        { return g_pit_ns; }
u32 pit_get_frequency(void) { return g_pit_freq; }

#include "../../src/kernel/sys/sys_io.c"

static int setup(void **state)
{
  (void)state;
  g_user_range_ok = true;
  g_vfs_read_ret  = 5;
  g_vfs_write_ret = 5;
  g_vfs_seek_ret  = 0;
  g_vfs_ioctl_ret = 0;
  g_vfs_sel_read  = 1;
  g_vfs_sel_write = 1;
  g_pit_ns        = 0;
  return 0;
}

/* sys_read */
static void read_efault_on_bad_ptr(void **state)
{
  (void)state;
  g_user_range_ok = false;
  char buf[8];
  assert_int_equal((i64)sys_read(3, (u64)buf, 8, 0, 0, 0), -EFAULT);
}

static void read_zero_count_returns_zero(void **state)
{
  (void)state;
  char buf[8];
  assert_int_equal(sys_read(3, (u64)buf, 0, 0, 0, 0), 0);
}

static void read_dispatches_to_vfs(void **state)
{
  (void)state;
  char buf[8];
  assert_int_equal(sys_read(3, (u64)buf, 8, 0, 0, 0), 5);
}

/* sys_write */
static void write_efault_on_bad_ptr(void **state)
{
  (void)state;
  g_user_range_ok = false;
  char buf[8] = "hello";
  assert_int_equal((i64)sys_write(1, (u64)buf, 5, 0, 0, 0), -EFAULT);
}

static void write_zero_count_returns_zero(void **state)
{
  (void)state;
  char buf[8] = "hi";
  assert_int_equal(sys_write(1, (u64)buf, 0, 0, 0, 0), 0);
}

static void write_dispatches_to_vfs(void **state)
{
  (void)state;
  char buf[8] = "hello";
  assert_int_equal(sys_write(1, (u64)buf, 5, 0, 0, 0), 5);
}

/* sys_lseek */
static void lseek_dispatches_to_vfs(void **state)
{
  (void)state;
  g_vfs_seek_ret = 100;
  assert_int_equal(sys_lseek(3, 100, 0, 0, 0, 0), 100);
}

/* sys_ioctl */
static void ioctl_dispatches_to_vfs(void **state)
{
  (void)state;
  assert_int_equal(sys_ioctl(3, 0x5401, 0, 0, 0, 0), 0);
}

/* sys_nanosleep */
static void nanosleep_efault_on_bad_ptr(void **state)
{
  (void)state;
  g_user_range_ok = false;
  assert_int_equal((i64)sys_nanosleep(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void nanosleep_zero_sleeps_immediately(void **state)
{
  (void)state;
  struct { i64 sec; i64 nsec; } ts = {0, 0};
  /* pit_get_ns returns 0; deadline = 0; loop body never executes */
  assert_int_equal(sys_nanosleep((u64)&ts, 0, 0, 0, 0, 0), 0);
}

static void nanosleep_negative_sec_einval(void **state)
{
  (void)state;
  struct { i64 sec; i64 nsec; } ts = {-1, 0};
  assert_int_equal((i64)sys_nanosleep((u64)&ts, 0, 0, 0, 0, 0), -EINVAL);
}

static void nanosleep_bad_nsec_einval(void **state)
{
  (void)state;
  struct { i64 sec; i64 nsec; } ts = {0, 2000000000LL};
  assert_int_equal((i64)sys_nanosleep((u64)&ts, 0, 0, 0, 0, 0), -EINVAL);
}

/* sys_readv */
struct test_iovec { void *base; u64 len; };

static void readv_zero_iovcnt_einval(void **state)
{
  (void)state;
  struct test_iovec v[1];
  assert_int_equal((i64)sys_readv(3, (u64)v, 0, 0, 0, 0), -EINVAL);
}

static void readv_null_ptr_efault(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_readv(3, 0, 1, 0, 0, 0), -EFAULT);
}

static void readv_reads_single_iov(void **state)
{
  (void)state;
  char buf[8];
  struct test_iovec v[1] = {{buf, 8}};
  g_vfs_read_ret = 5;
  assert_int_equal(sys_readv(3, (u64)v, 1, 0, 0, 0), 5);
}

static void readv_skips_null_base(void **state)
{
  (void)state;
  struct test_iovec v[2] = {{NULL, 4}, {NULL, 0}};
  assert_int_equal(sys_readv(3, (u64)v, 2, 0, 0, 0), 0);
}

/* sys_writev */
static void writev_null_iov_efault(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_writev(1, 0, 1, 0, 0, 0), -EFAULT);
}

static void writev_writes_single_iov(void **state)
{
  (void)state;
  char buf[8] = "hello";
  struct test_iovec v[1] = {{buf, 5}};
  g_vfs_write_ret = 5;
  assert_int_equal(sys_writev(1, (u64)v, 1, 0, 0, 0), 5);
}

static void writev_skips_null_base(void **state)
{
  (void)state;
  struct test_iovec v[2] = {{NULL, 4}, {NULL, 0}};
  assert_int_equal(sys_writev(1, (u64)v, 2, 0, 0, 0), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(read_efault_on_bad_ptr, setup),
      cmocka_unit_test_setup(read_zero_count_returns_zero, setup),
      cmocka_unit_test_setup(read_dispatches_to_vfs, setup),
      cmocka_unit_test_setup(write_efault_on_bad_ptr, setup),
      cmocka_unit_test_setup(write_zero_count_returns_zero, setup),
      cmocka_unit_test_setup(write_dispatches_to_vfs, setup),
      cmocka_unit_test_setup(lseek_dispatches_to_vfs, setup),
      cmocka_unit_test_setup(ioctl_dispatches_to_vfs, setup),
      cmocka_unit_test_setup(nanosleep_efault_on_bad_ptr, setup),
      cmocka_unit_test_setup(nanosleep_zero_sleeps_immediately, setup),
      cmocka_unit_test_setup(nanosleep_negative_sec_einval, setup),
      cmocka_unit_test_setup(nanosleep_bad_nsec_einval, setup),
      cmocka_unit_test_setup(readv_zero_iovcnt_einval, setup),
      cmocka_unit_test_setup(readv_null_ptr_efault, setup),
      cmocka_unit_test_setup(readv_reads_single_iov, setup),
      cmocka_unit_test_setup(readv_skips_null_base, setup),
      cmocka_unit_test_setup(writev_null_iov_efault, setup),
      cmocka_unit_test_setup(writev_writes_single_iov, setup),
      cmocka_unit_test_setup(writev_skips_null_base, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
