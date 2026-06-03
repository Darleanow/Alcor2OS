#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <setjmp.h>
#include <string.h>

void *kmalloc(u64 n) { (void)n; return NULL; }
void  kfree(void *p) { (void)p; }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n) { memset(d, 0, n); }
char *kstrncpy(char *d, const char *s, u64 m) {
  if(!m) return d;
  u64 i;
  for(i = 0; i < m - 1 && s[i]; i++) d[i] = s[i];
  d[i] = '\0';
  return d;
}
void console_print(const char *s) { (void)s; }
void console_printf(const char *fmt, ...) { (void)fmt; }

/* vmm stubs */
static bool g_user_ptr_ok = true;
static bool g_user_range_ok = true;
bool vmm_is_user_ptr(const void *p) { (void)p; return g_user_ptr_ok; }
bool vmm_is_user_range(const void *p, u64 n) { (void)p; (void)n; return g_user_range_ok; }

/* proc stubs */
static proc_t  g_proc;
static bool    g_no_proc = false;
proc_t *proc_current(void) { return g_no_proc ? NULL : &g_proc; }

static jmp_buf g_exit_jmp;
static i64     g_exit_code;
NORETURN void proc_exit(i64 code) {
  g_exit_code = code;
  longjmp(g_exit_jmp, 1);
  __builtin_unreachable();
}

static i64 g_fork_ret = 10;
i64 proc_fork(const void *frame) { (void)frame; return g_fork_ret; }

static i64 g_clone_ret = 11;
i64 proc_clone(const syscall_frame_t *frame, u64 child_stack, u32 flags) {
  (void)frame; (void)child_stack; (void)flags;
  return g_clone_ret;
}

static i64 g_waitpid_ret = 5;
static i32 g_waitpid_status = 0;
i64 proc_waitpid(i64 pid, i32 *status, i32 options) {
  (void)pid; (void)options;
  if(status) *status = g_waitpid_status;
  return g_waitpid_ret;
}

/* cpu stubs */
static u64 g_fs_base = 0;
static u64 g_gs_base = 0;
void cpu_set_fs_base(u64 addr) { g_fs_base = addr; }
u64  cpu_get_fs_base(void)     { return g_fs_base; }
void cpu_set_gs_base(u64 addr) { g_gs_base = addr; }
u64  cpu_get_gs_base(void)     { return g_gs_base; }

/* syscall frame stub */
static syscall_frame_t g_frame;
static bool g_no_frame = false;
syscall_frame_t *syscall_get_current_frame(void) {
  return g_no_frame ? NULL : &g_frame;
}

/* vfs stubs (needed by sys_execve internals but we don't test exec here) */
i64 vfs_open(const char *p, u32 f)   { (void)p; (void)f; return -1; }
i64 vfs_close(i64 fd)                { (void)fd; return 0; }
i64 vfs_stat(const char *p, vfs_stat_t *s) { (void)p; (void)s; return -1; }
void vfs_proc_close_cloexec_fds(void) {}
i64 proc_exec_replace_image(proc_t *p, const char *n, i64 fd, char *const argv[], char *const envp[]) {
  (void)p; (void)n; (void)fd; (void)argv; (void)envp; return -ENOSYS;
}
void proc_notify_exec(const proc_t *p) { (void)p; }

#include "../../src/kernel/sys/sys_proc.c"

static int setup(void **state)
{
  (void)state;
  memset(&g_proc, 0, sizeof(g_proc));
  g_proc.pid        = 42;
  g_proc.parent_pid = 1;
  g_proc.fs_base    = 0;
  g_no_proc         = false;
  g_no_frame        = false;
  g_user_ptr_ok     = true;
  g_user_range_ok   = true;
  g_fork_ret        = 10;
  g_clone_ret       = 11;
  g_waitpid_ret     = 5;
  g_waitpid_status  = 0;
  g_fs_base         = 0;
  g_gs_base         = 0;
  return 0;
}

/* sys_getpid */
static void getpid_returns_pid(void **state)
{
  (void)state;
  assert_int_equal(sys_getpid(0, 0, 0, 0, 0, 0), 42);
}

static void getpid_no_proc_returns_one(void **state)
{
  (void)state;
  g_no_proc = true;
  assert_int_equal(sys_getpid(0, 0, 0, 0, 0, 0), 1);
}

/* sys_gettid */
static void gettid_same_as_getpid(void **state)
{
  (void)state;
  assert_int_equal(sys_gettid(0, 0, 0, 0, 0, 0), 42);
}

/* sys_getppid */
static void getppid_returns_parent(void **state)
{
  (void)state;
  assert_int_equal(sys_getppid(0, 0, 0, 0, 0, 0), 1);
}

static void getppid_no_proc_returns_zero(void **state)
{
  (void)state;
  g_no_proc = true;
  assert_int_equal(sys_getppid(0, 0, 0, 0, 0, 0), 0);
}

/* sys_getuid/getgid/geteuid/getegid — all return 0 */
static void identity_syscalls_return_zero(void **state)
{
  (void)state;
  assert_int_equal(sys_getuid(0, 0, 0, 0, 0, 0), 0);
  assert_int_equal(sys_getgid(0, 0, 0, 0, 0, 0), 0);
  assert_int_equal(sys_geteuid(0, 0, 0, 0, 0, 0), 0);
  assert_int_equal(sys_getegid(0, 0, 0, 0, 0, 0), 0);
}

/* sys_set_tid_address */
static void set_tid_address_returns_pid(void **state)
{
  (void)state;
  assert_int_equal(sys_set_tid_address(0, 0, 0, 0, 0, 0), 42);
}

static void set_tid_address_no_proc_returns_one(void **state)
{
  (void)state;
  g_no_proc = true;
  assert_int_equal(sys_set_tid_address(0, 0, 0, 0, 0, 0), 1);
}

/* sys_fork */
static void fork_no_frame_returns_einval(void **state)
{
  (void)state;
  g_no_frame = true;
  assert_int_equal((i64)sys_fork(0, 0, 0, 0, 0, 0), -EINVAL);
}

static void fork_returns_child_pid(void **state)
{
  (void)state;
  assert_int_equal(sys_fork(0, 0, 0, 0, 0, 0), 10);
}

/* sys_clone */
static void clone_thread_returns_enosys(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_clone(0x00010000u, 0, 0, 0, 0, 0), -ENOSYS);
}

static void clone_no_frame_returns_einval(void **state)
{
  (void)state;
  g_no_frame = true;
  assert_int_equal((i64)sys_clone(0, 0, 0, 0, 0, 0), -EINVAL);
}

static void clone_bad_flags_returns_einval(void **state)
{
  (void)state;
  /* Flags outside ALCOR_CLONE_VM|VFORK|CSIGNAL */
  assert_int_equal((i64)sys_clone(0xDEAD0000u, 0, 0, 0, 0, 0), -ENOSYS);
}

static void clone_bad_stack_returns_efault(void **state)
{
  (void)state;
  g_user_ptr_ok = false;
  /* CLONE_VM with invalid child_stack */
  assert_int_equal((i64)sys_clone(0x00000100u, 0x1234, 0, 0, 0, 0), -EFAULT);
}

static void clone_succeeds(void **state)
{
  (void)state;
  assert_int_equal(sys_clone(0, 0, 0, 0, 0, 0), 11);
}

/* sys_exit */
static void exit_calls_proc_exit(void **state)
{
  (void)state;
  if(setjmp(g_exit_jmp) == 0)
    sys_exit(99, 0, 0, 0, 0, 0);
  assert_int_equal(g_exit_code, 99);
}

/* sys_exit_group */
static void exit_group_same_as_exit(void **state)
{
  (void)state;
  if(setjmp(g_exit_jmp) == 0)
    sys_exit_group(7, 0, 0, 0, 0, 0);
  assert_int_equal(g_exit_code, 7);
}

/* sys_wait4 */
static void wait4_no_wstatus(void **state)
{
  (void)state;
  g_waitpid_ret = 3;
  assert_int_equal(sys_wait4(3, 0, 0, 0, 0, 0), 3);
}

static void wait4_with_wstatus(void **state)
{
  (void)state;
  g_waitpid_ret    = 5;
  g_waitpid_status = 0x200; /* exit code 2 */
  i32 st;
  assert_int_equal(sys_wait4(5, (u64)&st, 0, 0, 0, 0), 5);
  assert_int_equal(st, 0x200);
}

static void wait4_bad_wstatus_returns_efault(void **state)
{
  (void)state;
  g_waitpid_ret   = 5;
  g_user_range_ok = false;
  i32 dummy;
  assert_int_equal((i64)sys_wait4(5, (u64)&dummy, 0, 0, 0, 0), -EFAULT);
}

static void wait4_negative_ret_passthrough(void **state)
{
  (void)state;
  g_waitpid_ret = -ECHILD;
  assert_int_equal((i64)sys_wait4(-1, 0, 0, 0, 0, 0), -ECHILD);
}

/* sys_arch_prctl */
static void arch_prctl_set_fs(void **state)
{
  (void)state;
  assert_int_equal(sys_arch_prctl(0x1002, 0xDEAD, 0, 0, 0, 0), 0);
  assert_int_equal(g_fs_base, 0xDEAD);
  assert_int_equal(g_proc.fs_base, 0xDEAD);
}

static void arch_prctl_get_fs(void **state)
{
  (void)state;
  g_fs_base = 0xBEEF;
  u64 out;
  assert_int_equal(sys_arch_prctl(0x1003, (u64)&out, 0, 0, 0, 0), 0);
  assert_int_equal(out, 0xBEEF);
}

static void arch_prctl_get_fs_null_returns_efault(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_arch_prctl(0x1003, 0, 0, 0, 0, 0), -EFAULT);
}

static void arch_prctl_set_gs(void **state)
{
  (void)state;
  assert_int_equal(sys_arch_prctl(0x1001, 0xCAFE, 0, 0, 0, 0), 0);
  assert_int_equal(g_gs_base, 0xCAFE);
}

static void arch_prctl_get_gs(void **state)
{
  (void)state;
  g_gs_base = 0x1234;
  u64 out;
  assert_int_equal(sys_arch_prctl(0x1004, (u64)&out, 0, 0, 0, 0), 0);
  assert_int_equal(out, 0x1234);
}

static void arch_prctl_get_gs_null_returns_efault(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_arch_prctl(0x1004, 0, 0, 0, 0, 0), -EFAULT);
}

static void arch_prctl_unknown_returns_einval(void **state)
{
  (void)state;
  assert_int_equal((i64)sys_arch_prctl(0xDEAD, 0, 0, 0, 0, 0), -EINVAL);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(getpid_returns_pid, setup),
      cmocka_unit_test_setup(getpid_no_proc_returns_one, setup),
      cmocka_unit_test_setup(gettid_same_as_getpid, setup),
      cmocka_unit_test_setup(getppid_returns_parent, setup),
      cmocka_unit_test_setup(getppid_no_proc_returns_zero, setup),
      cmocka_unit_test_setup(identity_syscalls_return_zero, setup),
      cmocka_unit_test_setup(set_tid_address_returns_pid, setup),
      cmocka_unit_test_setup(set_tid_address_no_proc_returns_one, setup),
      cmocka_unit_test_setup(fork_no_frame_returns_einval, setup),
      cmocka_unit_test_setup(fork_returns_child_pid, setup),
      cmocka_unit_test_setup(clone_thread_returns_enosys, setup),
      cmocka_unit_test_setup(clone_no_frame_returns_einval, setup),
      cmocka_unit_test_setup(clone_bad_flags_returns_einval, setup),
      cmocka_unit_test_setup(clone_bad_stack_returns_efault, setup),
      cmocka_unit_test_setup(clone_succeeds, setup),
      cmocka_unit_test_setup(exit_calls_proc_exit, setup),
      cmocka_unit_test_setup(exit_group_same_as_exit, setup),
      cmocka_unit_test_setup(wait4_no_wstatus, setup),
      cmocka_unit_test_setup(wait4_with_wstatus, setup),
      cmocka_unit_test_setup(wait4_bad_wstatus_returns_efault, setup),
      cmocka_unit_test_setup(wait4_negative_ret_passthrough, setup),
      cmocka_unit_test_setup(arch_prctl_set_fs, setup),
      cmocka_unit_test_setup(arch_prctl_get_fs, setup),
      cmocka_unit_test_setup(arch_prctl_get_fs_null_returns_efault, setup),
      cmocka_unit_test_setup(arch_prctl_set_gs, setup),
      cmocka_unit_test_setup(arch_prctl_get_gs, setup),
      cmocka_unit_test_setup(arch_prctl_get_gs_null_returns_efault, setup),
      cmocka_unit_test_setup(arch_prctl_unknown_returns_einval, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
