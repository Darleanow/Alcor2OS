/**
 * @file src/kernel/sys/sys_dispatch.c
 * @brief Numbered syscall lookup and dispatch.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>
#include <alcor2/sys/internal.h>
#include <alcor2/sys/syscall.h>

/* Set to 1 to trace every syscall with its arguments */
#define SYS_TRACE 0

#define SYS_DEF(n, nm, fn)                                                     \
  {                                                                            \
    (n), (nm), (fn)                                                            \
  }
#define SYS_END                                                                \
  {                                                                            \
    0, NULL, NULL                                                              \
  }

/**
 * @brief Table of all supported syscalls.
 *
 * Ordered logically but uses explicit indices for RAX mapping.
 * Terminated by ::SYS_END sentinel.
 */
static const sys_def_t sys_table[] = {
    SYS_DEF(SYS_READ, "read", sys_read),
    SYS_DEF(SYS_WRITE, "write", sys_write),
    SYS_DEF(SYS_OPEN, "open", sys_open),
    SYS_DEF(SYS_CLOSE, "close", sys_close),
    SYS_DEF(SYS_STAT, "stat", sys_stat),
    SYS_DEF(SYS_FSTAT, "fstat", sys_fstat),
    SYS_DEF(SYS_LSTAT, "lstat", sys_lstat),
    SYS_DEF(SYS_POLL, "poll", sys_poll),
    SYS_DEF(SYS_LSEEK, "lseek", sys_lseek),
    SYS_DEF(SYS_MMAP, "mmap", sys_mmap),
    SYS_DEF(SYS_MPROTECT, "mprotect", sys_mprotect),
    SYS_DEF(SYS_MUNMAP, "munmap", sys_munmap),
    SYS_DEF(SYS_BRK, "brk", sys_brk),
    SYS_DEF(SYS_RT_SIGACTION, "rt_sigaction", sys_rt_sigaction),
    SYS_DEF(SYS_RT_SIGPROCMASK, "rt_sigprocmask", sys_rt_sigprocmask),
    SYS_DEF(SYS_RT_SIGRETURN, "rt_sigreturn", sys_rt_sigreturn),
    SYS_DEF(SYS_IOCTL, "ioctl", sys_ioctl),
    SYS_DEF(SYS_PREAD64, "pread64", sys_pread64),
    SYS_DEF(SYS_PWRITE64, "pwrite64", sys_pwrite64),
    SYS_DEF(SYS_READV, "readv", sys_readv),
    SYS_DEF(SYS_WRITEV, "writev", sys_writev),
    SYS_DEF(SYS_ACCESS, "access", sys_access),
    SYS_DEF(SYS_PIPE, "pipe", sys_pipe),
    SYS_DEF(SYS_SELECT, "select", sys_select),
    SYS_DEF(SYS_SCHED_YIELD, "sched_yield", sys_sched_yield),
    SYS_DEF(SYS_DUP, "dup", sys_dup),
    SYS_DEF(SYS_DUP2, "dup2", sys_dup2),
    SYS_DEF(SYS_NANOSLEEP, "nanosleep", sys_nanosleep),
    SYS_DEF(SYS_GETPID, "getpid", sys_getpid),
    SYS_DEF(SYS_CLONE, "clone", sys_clone),
    SYS_DEF(SYS_FORK, "fork", sys_fork),
    SYS_DEF(SYS_EXECVE, "execve", sys_execve),
    SYS_DEF(SYS_EXIT, "exit", sys_exit),
    SYS_DEF(SYS_WAIT4, "wait4", sys_wait4),
    SYS_DEF(SYS_KILL, "kill", sys_kill),
    SYS_DEF(SYS_UNAME, "uname", sys_uname),
    SYS_DEF(SYS_FCNTL, "fcntl", sys_fcntl),
    SYS_DEF(SYS_FTRUNCATE, "ftruncate", sys_ftruncate),
    SYS_DEF(SYS_GETDENTS, "getdents", sys_getdents),
    SYS_DEF(SYS_GETCWD, "getcwd", sys_getcwd),
    SYS_DEF(SYS_CHDIR, "chdir", sys_chdir),
    SYS_DEF(SYS_MKDIR, "mkdir", sys_mkdir),
    SYS_DEF(SYS_RMDIR, "rmdir", sys_rmdir),
    SYS_DEF(SYS_CREAT, "creat", sys_creat),
    SYS_DEF(SYS_UNLINK, "unlink", sys_unlink),
    SYS_DEF(SYS_SYMLINK, "symlink", sys_symlink),
    SYS_DEF(SYS_READLINK, "readlink", sys_readlink),
    SYS_DEF(SYS_RENAME, "rename", sys_rename),
    SYS_DEF(SYS_GETTIMEOFDAY, "gettimeofday", sys_gettimeofday),
    SYS_DEF(SYS_GETRLIMIT, "getrlimit", sys_getrlimit),
    SYS_DEF(SYS_ARCH_PRCTL, "arch_prctl", sys_arch_prctl),
    SYS_DEF(SYS_GETTID, "gettid", sys_gettid),
    SYS_DEF(SYS_GETPPID, "getppid", sys_getppid),
    SYS_DEF(SYS_FUTEX, "futex", sys_futex),
    SYS_DEF(SYS_SCHED_GETAFFINITY, "sched_getaffinity", sys_sched_getaffinity),
    SYS_DEF(SYS_GETDENTS64, "getdents64", sys_getdents64),
    SYS_DEF(SYS_CLOCK_GETTIME, "clock_gettime", sys_clock_gettime),
    SYS_DEF(SYS_EXIT_GROUP, "exit_group", sys_exit_group),
    SYS_DEF(SYS_OPENAT, "openat", sys_openat),
    SYS_DEF(SYS_NEWFSTATAT, "newfstatat", sys_newfstatat),
    SYS_DEF(SYS_FACCESSAT, "faccessat", sys_faccessat),
    SYS_DEF(SYS_PIPE2, "pipe2", sys_pipe2),
    SYS_DEF(SYS_SIGALTSTACK, "sigaltstack", sys_sigaltstack),
    SYS_DEF(SYS_SET_TID_ADDRESS, "set_tid_address", sys_set_tid_address),
    SYS_DEF(SYS_PRLIMIT64, "prlimit64", sys_prlimit64),
    SYS_DEF(SYS_GETUID, "getuid", sys_getuid),
    SYS_DEF(SYS_GETGID, "getgid", sys_getgid),
    SYS_DEF(SYS_GETEUID, "geteuid", sys_geteuid),
    SYS_DEF(SYS_GETEGID, "getegid", sys_getegid),
    SYS_DEF(SYS_TKILL, "tkill", sys_tkill),
    SYS_DEF(SYS_TGKILL, "tgkill", sys_tgkill),
    SYS_DEF(SYS_ALCOR_FB_INFO, "alcor_fb_info", sys_alcor_fb_info),
    SYS_DEF(SYS_ALCOR_FB_MMAP, "alcor_fb_mmap", sys_alcor_fb_mmap),
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
    console_printf("[sys] unknown syscall %d\n", (int)num);
#endif
    if(p)
      p->current_frame = old_frame;
    return (u64)-ENOSYS;
  }

#if SYS_TRACE
  console_printf(
      "[sys] %s(%lx, %lx, %lx, %lx, %lx, %lx)", d->name, frame->rdi, frame->rsi,
      frame->rdx, frame->r10, frame->r8, frame->r9
  );
#endif

  u64 ret = d->handler(
      frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9
  );

#if SYS_TRACE
  console_printf(" = %lx\n", ret);
#endif

  if(p)
    p->current_frame = old_frame;

  /* Check if we need to switch tasks before returning to user mode. */
  sched_check_resched();

  return ret;
}
