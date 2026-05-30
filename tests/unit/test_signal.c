#include "test_common.h"

#include <alcor2/proc/proc.h>
#include <alcor2/proc/signal.h>
#include <alcor2/types.h>

#include <stdlib.h>
#include <string.h>

void console_print(const char *s)
{
  (void)s;
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}
void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
bool vmm_is_user_range(const void *p, u64 n)
{
  (void)p;
  (void)n;
  return true;
}
syscall_frame_t *syscall_get_current_frame(void)
{
  return NULL;
}

static proc_t g_proc;
proc_t       *proc_current(void)
{
  return &g_proc;
}
proc_t *proc_get(u64 pid)
{
  if(pid == g_proc.pid)
    return &g_proc;
  return NULL;
}
void proc_exit(i64 c)
{
  (void)c;
}
void proc_wake(proc_t *p)
{
  if(p && p->state == PROC_STATE_BLOCKED)
    p->state = PROC_STATE_READY;
}
void proc_block(proc_t *p)
{
  if(p)
    p->state = PROC_STATE_BLOCKED;
}
void proc_schedule(void) {}

#include "../../src/kernel/sys/signal.c"

static int setup(void **state)
{
  (void)state;
  memset(&g_proc, 0, sizeof(g_proc));
  g_proc.pid      = 1;
  g_proc.state    = PROC_STATE_RUNNING;
  g_proc.sig_mask = 0;
  return 0;
}

static void sig_default_ignore_known_signals(void **state)
{
  (void)state;
  assert_int_equal(sig_default_ignore(SIGCHLD), 1);
  assert_int_equal(sig_default_ignore(SIGWINCH), 1);
  assert_int_equal(sig_default_ignore(SIGCONT), 1);
  assert_int_equal(sig_default_ignore(SIGURG), 1);
}

static void sig_default_ignore_fatal_signals(void **state)
{
  (void)state;
  assert_int_equal(sig_default_ignore(SIGKILL), 0);
  assert_int_equal(sig_default_ignore(SIGSEGV), 0);
  assert_int_equal(sig_default_ignore(SIGTERM), 0);
  assert_int_equal(sig_default_ignore(1), 0);
}

static void proc_signal_sets_pending_bit(void **state)
{
  (void)state;
  proc_signal(g_proc.pid, SIGUSR1);
  assert_true(g_proc.sig_pending & (1ULL << SIGUSR1));
}

static void proc_signal_wakes_blocked_proc(void **state)
{
  (void)state;
  g_proc.state = PROC_STATE_BLOCKED;
  proc_signal(g_proc.pid, SIGUSR1);
  assert_int_equal(g_proc.state, PROC_STATE_READY);
}

static void proc_signal_ignores_unknown_pid(void **state)
{
  (void)state;
  proc_signal(9999, SIGUSR1);
}

static void proc_signal_rejects_signum_zero(void **state)
{
  (void)state;
  proc_signal(g_proc.pid, 0);
  assert_int_equal(g_proc.sig_pending, 0);
}

static void proc_signal_rejects_signum_nsig(void **state)
{
  (void)state;
  proc_signal(g_proc.pid, NSIG);
  assert_int_equal(g_proc.sig_pending, 0);
}

static void sigprocmask_block(void **state)
{
  (void)state;
  u64 set = 1ULL << SIGUSR1;
  sys_rt_sigprocmask(SIG_BLOCK, (u64)&set, 0, 8, 0, 0);
  assert_true(g_proc.sig_mask & (1ULL << SIGUSR1));
}

static void sigprocmask_unblock(void **state)
{
  (void)state;
  g_proc.sig_mask = 1ULL << SIGUSR1;
  u64 set         = 1ULL << SIGUSR1;
  sys_rt_sigprocmask(SIG_UNBLOCK, (u64)&set, 0, 8, 0, 0);
  assert_false(g_proc.sig_mask & (1ULL << SIGUSR1));
}

static void sigprocmask_setmask(void **state)
{
  (void)state;
  u64 set = (1ULL << SIGUSR1) | (1ULL << SIGUSR2);
  sys_rt_sigprocmask(SIG_SETMASK, (u64)&set, 0, 8, 0, 0);
  assert_int_equal(g_proc.sig_mask, set);
}

static void sigprocmask_cannot_block_sigkill(void **state)
{
  (void)state;
  u64 set = 1ULL << SIGKILL;
  sys_rt_sigprocmask(SIG_BLOCK, (u64)&set, 0, 8, 0, 0);
  assert_false(g_proc.sig_mask & (1ULL << SIGKILL));
}

static void sigprocmask_cannot_block_sigstop(void **state)
{
  (void)state;
  u64 set = 1ULL << SIGSTOP;
  sys_rt_sigprocmask(SIG_BLOCK, (u64)&set, 0, 8, 0, 0);
  assert_false(g_proc.sig_mask & (1ULL << SIGSTOP));
}

static void sigprocmask_wrong_sigsetsize(void **state)
{
  (void)state;
  u64 set = 0;
  u64 ret = sys_rt_sigprocmask(SIG_BLOCK, (u64)&set, 0, 4, 0, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void sigprocmask_saves_oldset(void **state)
{
  (void)state;
  g_proc.sig_mask = 1ULL << SIGUSR2;
  u64 old         = 0;
  sys_rt_sigprocmask(SIG_BLOCK, 0, (u64)&old, 8, 0, 0);
  assert_int_equal(old, 1ULL << SIGUSR2);
}

static void sigprocmask_bad_how(void **state)
{
  (void)state;
  u64 set = 0;
  u64 ret = sys_rt_sigprocmask(99, (u64)&set, 0, 8, 0, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void sys_kill_signal_zero_is_existence_check(void **state)
{
  (void)state;
  u64 ret = sys_kill(g_proc.pid, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
  assert_int_equal(g_proc.sig_pending, 0);
}

static void sys_kill_unknown_pid_returns_esrch(void **state)
{
  (void)state;
  u64 ret = sys_kill(9999, SIGUSR1, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ESRCH);
}

static void sys_kill_invalid_sig_returns_einval(void **state)
{
  (void)state;
  u64 ret = sys_kill(g_proc.pid, NSIG, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void sys_kill_sends_signal(void **state)
{
  (void)state;
  sys_kill(g_proc.pid, SIGUSR1, 0, 0, 0, 0);
  assert_true(g_proc.sig_pending & (1ULL << SIGUSR1));
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(sig_default_ignore_known_signals, setup),
      cmocka_unit_test_setup(sig_default_ignore_fatal_signals, setup),
      cmocka_unit_test_setup(proc_signal_sets_pending_bit, setup),
      cmocka_unit_test_setup(proc_signal_wakes_blocked_proc, setup),
      cmocka_unit_test_setup(proc_signal_ignores_unknown_pid, setup),
      cmocka_unit_test_setup(proc_signal_rejects_signum_zero, setup),
      cmocka_unit_test_setup(proc_signal_rejects_signum_nsig, setup),
      cmocka_unit_test_setup(sigprocmask_block, setup),
      cmocka_unit_test_setup(sigprocmask_unblock, setup),
      cmocka_unit_test_setup(sigprocmask_setmask, setup),
      cmocka_unit_test_setup(sigprocmask_cannot_block_sigkill, setup),
      cmocka_unit_test_setup(sigprocmask_cannot_block_sigstop, setup),
      cmocka_unit_test_setup(sigprocmask_wrong_sigsetsize, setup),
      cmocka_unit_test_setup(sigprocmask_saves_oldset, setup),
      cmocka_unit_test_setup(sigprocmask_bad_how, setup),
      cmocka_unit_test_setup(sys_kill_signal_zero_is_existence_check, setup),
      cmocka_unit_test_setup(sys_kill_unknown_pid_returns_esrch, setup),
      cmocka_unit_test_setup(sys_kill_invalid_sig_returns_einval, setup),
      cmocka_unit_test_setup(sys_kill_sends_signal, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
