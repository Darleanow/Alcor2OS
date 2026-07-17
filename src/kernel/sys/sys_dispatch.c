/**
 * @file src/kernel/sys/sys_dispatch.c
 * @brief Numbered syscall lookup and dispatch.
 */

#include <alcor2/drivers/klog.h>
#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/sys/syscall.h>

/* Set to 1 to trace every syscall with its arguments */
/* Build-time toggle: pass SYS_TRACE=1 via CFLAGS (e.g. `make run-trace`)
 * to log every syscall with name, args, and return value to debugcon. */
#ifndef SYS_TRACE
  #define SYS_TRACE 0
#endif

#define SYS_DEF(n, nm, fn) {(n), (nm), (fn)}
#define SYS_END            {0, NULL, NULL}

/**
 * @brief Table of all supported syscalls.
 *
 * Ordered logically but uses explicit indices for RAX mapping.
 * Terminated by ::SYS_END sentinel.
 */
static const sys_def_t sys_table[] = {
    SYS_DEF(__NR_read, "read", sys_read),
    SYS_DEF(__NR_write, "write", sys_write),
    SYS_DEF(__NR_open, "open", sys_open),
    SYS_DEF(__NR_close, "close", sys_close),
    SYS_DEF(__NR_stat, "stat", sys_stat),
    SYS_DEF(__NR_fstat, "fstat", sys_fstat),
    SYS_DEF(__NR_lstat, "lstat", sys_lstat),
    SYS_DEF(__NR_poll, "poll", sys_poll),
    SYS_DEF(__NR_lseek, "lseek", sys_lseek),
    SYS_DEF(__NR_mmap, "mmap", sys_mmap),
    SYS_DEF(__NR_mprotect, "mprotect", sys_mprotect),
    SYS_DEF(__NR_munmap, "munmap", sys_munmap),
    SYS_DEF(__NR_brk, "brk", sys_brk),
    SYS_DEF(__NR_rt_sigaction, "rt_sigaction", sys_rt_sigaction),
    SYS_DEF(__NR_rt_sigprocmask, "rt_sigprocmask", sys_rt_sigprocmask),
    SYS_DEF(__NR_rt_sigreturn, "rt_sigreturn", sys_rt_sigreturn),
    SYS_DEF(__NR_ioctl, "ioctl", sys_ioctl),
    SYS_DEF(__NR_pread64, "pread64", sys_pread64),
    SYS_DEF(__NR_pwrite64, "pwrite64", sys_pwrite64),
    SYS_DEF(__NR_readv, "readv", sys_readv),
    SYS_DEF(__NR_writev, "writev", sys_writev),
    SYS_DEF(__NR_access, "access", sys_access),
    SYS_DEF(__NR_pipe, "pipe", sys_pipe),
    SYS_DEF(__NR_select, "select", sys_select),
    SYS_DEF(__NR_sched_yield, "sched_yield", sys_sched_yield),
    SYS_DEF(__NR_dup, "dup", sys_dup),
    SYS_DEF(__NR_dup2, "dup2", sys_dup2),
    SYS_DEF(__NR_nanosleep, "nanosleep", sys_nanosleep),
    SYS_DEF(__NR_getpid, "getpid", sys_getpid),
    SYS_DEF(__NR_clone, "clone", sys_clone),
    SYS_DEF(__NR_fork, "fork", sys_fork),
    SYS_DEF(__NR_execve, "execve", sys_execve),
    SYS_DEF(__NR_exit, "exit", sys_exit),
    SYS_DEF(__NR_wait4, "wait4", sys_wait4),
    SYS_DEF(__NR_kill, "kill", sys_kill),
    SYS_DEF(__NR_uname, "uname", sys_uname),
    SYS_DEF(__NR_fcntl, "fcntl", sys_fcntl),
    SYS_DEF(__NR_ftruncate, "ftruncate", sys_ftruncate),
    SYS_DEF(__NR_getdents, "getdents", sys_getdents),
    SYS_DEF(__NR_getcwd, "getcwd", sys_getcwd),
    SYS_DEF(__NR_chdir, "chdir", sys_chdir),
    SYS_DEF(__NR_mkdir, "mkdir", sys_mkdir),
    SYS_DEF(__NR_rmdir, "rmdir", sys_rmdir),
    SYS_DEF(__NR_creat, "creat", sys_creat),
    SYS_DEF(__NR_unlink, "unlink", sys_unlink),
    SYS_DEF(__NR_symlink, "symlink", sys_symlink),
    SYS_DEF(__NR_readlink, "readlink", sys_readlink),
    SYS_DEF(__NR_rename, "rename", sys_rename),
    SYS_DEF(__NR_gettimeofday, "gettimeofday", sys_gettimeofday),
    SYS_DEF(__NR_getrlimit, "getrlimit", sys_getrlimit),
    SYS_DEF(__NR_arch_prctl, "arch_prctl", sys_arch_prctl),
    SYS_DEF(__NR_gettid, "gettid", sys_gettid),
    SYS_DEF(__NR_getppid, "getppid", sys_getppid),
    SYS_DEF(__NR_futex, "futex", sys_futex),
    SYS_DEF(__NR_sched_getaffinity, "sched_getaffinity", sys_sched_getaffinity),
    SYS_DEF(__NR_getdents64, "getdents64", sys_getdents64),
    SYS_DEF(__NR_clock_gettime, "clock_gettime", sys_clock_gettime),
    SYS_DEF(__NR_exit_group, "exit_group", sys_exit_group),
    SYS_DEF(__NR_openat, "openat", sys_openat),
    SYS_DEF(__NR_newfstatat, "newfstatat", sys_newfstatat),
    SYS_DEF(__NR_faccessat, "faccessat", sys_faccessat),
    SYS_DEF(__NR_pipe2, "pipe2", sys_pipe2),
    SYS_DEF(__NR_sigaltstack, "sigaltstack", sys_sigaltstack),
    SYS_DEF(__NR_set_tid_address, "set_tid_address", sys_set_tid_address),
    SYS_DEF(__NR_prlimit64, "prlimit64", sys_prlimit64),
    SYS_DEF(__NR_getuid, "getuid", sys_getuid),
    SYS_DEF(__NR_getgid, "getgid", sys_getgid),
    SYS_DEF(__NR_geteuid, "geteuid", sys_geteuid),
    SYS_DEF(__NR_getegid, "getegid", sys_getegid),
    SYS_DEF(__NR_tkill, "tkill", sys_tkill),
    SYS_DEF(__NR_tgkill, "tgkill", sys_tgkill),
    SYS_DEF(__NR_ALCOR_FB_INFO, "alcor_fb_info", sys_alcor_fb_info),
    SYS_DEF(__NR_ALCOR_FB_MMAP, "alcor_fb_mmap", sys_alcor_fb_mmap),
    SYS_DEF(__NR_ALCOR_SET_FG_PID, "alcor_set_fg_pid", sys_alcor_set_fg_pid),
    SYS_END
};

/**
 * @brief Return the syscall_frame_t for the currently executing syscall.
 */
syscall_frame_t *syscall_get_current_frame(void)
{
  proc_t *p = proc_current();
  return p ? p->current_frame : NULL;
}

static const sys_def_t *sys__find(u64 num)
{
  for(const sys_def_t *d = sys_table; d->name != NULL; d++) {
    if(d->num == num)
      return d;
  }
  return NULL;
}

/**
 * @brief Dispatch a syscall from the architecture-specific entry point.
 *
 * Implements optional tracing and lookup through the declarative syscall table.
 */
u64 syscall_dispatch(syscall_frame_t *frame)
{
  u64              num = frame->rax;
  const sys_def_t *d   = sys__find(num);

  proc_t          *p         = proc_current();
  syscall_frame_t *old_frame = NULL;
  if(p) {
    old_frame        = p->current_frame;
    p->current_frame = frame;
  }

  if(!d || !d->handler) {
#if SYS_TRACE
    klogf("[sys] unknown syscall %d\n", (int)num);
#endif
    if(p)
      p->current_frame = old_frame;
    return (u64)-ENOSYS;
  }

#if SYS_TRACE
  klogf(
      "[sys] %s(%lx, %lx, %lx, %lx, %lx, %lx)", d->name, frame->rdi, frame->rsi,
      frame->rdx, frame->r10, frame->r8, frame->r9
  );
#endif

  u64 ret = d->handler(
      frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9
  );

#if SYS_TRACE
  klogf(" = %lx\n", ret);
#endif

  if(p)
    p->current_frame = old_frame;

  /* Check if we need to switch tasks before returning to user mode. */
  proc_check_resched();

  return ret;
}
