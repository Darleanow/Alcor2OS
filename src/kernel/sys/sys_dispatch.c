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

#define SYS_DEF(n, nm, na, fn)                                                 \
  {                                                                            \
    (n), (nm), (na), (fn)                                                      \
  }
#define SYS_END                                                                \
  {                                                                            \
    0, NULL, 0, NULL                                                           \
  }

/**
 * @brief Table of all supported syscalls.
 *
 * Ordered logically but uses explicit indices for RAX mapping.
 * Terminated by ::SYS_END sentinel.
 */
static const sys_def_t sys_table[] = {
    SYS_DEF(SYS_READ, "read", 3, sys_read),
    SYS_DEF(SYS_WRITE, "write", 3, sys_write),
    SYS_DEF(SYS_OPEN, "open", 3, sys_open),
    SYS_DEF(SYS_CLOSE, "close", 1, sys_close),
    SYS_DEF(SYS_STAT, "stat", 2, sys_stat),
    SYS_DEF(SYS_FSTAT, "fstat", 2, sys_fstat),
    SYS_DEF(SYS_LSTAT, "lstat", 2, sys_lstat),
    SYS_DEF(SYS_POLL, "poll", 3, sys_poll),
    SYS_DEF(SYS_LSEEK, "lseek", 3, sys_lseek),
    SYS_DEF(SYS_MMAP, "mmap", 6, sys_mmap),
    SYS_DEF(SYS_MPROTECT, "mprotect", 3, sys_mprotect),
    SYS_DEF(SYS_MUNMAP, "munmap", 2, sys_munmap),
    SYS_DEF(SYS_BRK, "brk", 1, sys_brk),
    SYS_DEF(SYS_RT_SIGACTION, "rt_sigaction", 4, sys_rt_sigaction),
    SYS_DEF(SYS_RT_SIGPROCMASK, "rt_sigprocmask", 4, sys_rt_sigprocmask),
    SYS_DEF(SYS_RT_SIGRETURN, "rt_sigreturn", 0, sys_rt_sigreturn),
    SYS_DEF(SYS_IOCTL, "ioctl", 3, sys_ioctl),
    SYS_DEF(SYS_PREAD64, "pread64", 4, sys_pread64),
    SYS_DEF(SYS_PWRITE64, "pwrite64", 4, sys_pwrite64),
    SYS_DEF(SYS_READV, "readv", 3, sys_readv),
    SYS_DEF(SYS_WRITEV, "writev", 3, sys_writev),
    SYS_DEF(SYS_ACCESS, "access", 2, sys_access),
    SYS_DEF(SYS_PIPE, "pipe", 1, sys_pipe),
    SYS_DEF(SYS_SELECT, "select", 5, sys_select),
    SYS_DEF(SYS_SCHED_YIELD, "sched_yield", 0, sys_sched_yield),
    SYS_DEF(SYS_DUP, "dup", 1, sys_dup),
    SYS_DEF(SYS_DUP2, "dup2", 2, sys_dup2),
    SYS_DEF(SYS_NANOSLEEP, "nanosleep", 2, sys_nanosleep),
    SYS_DEF(SYS_GETPID, "getpid", 0, sys_getpid),
    SYS_DEF(SYS_CLONE, "clone", 5, sys_clone),
    SYS_DEF(SYS_FORK, "fork", 0, sys_fork),
    SYS_DEF(SYS_EXECVE, "execve", 3, sys_execve),
    SYS_DEF(SYS_EXIT, "exit", 1, sys_exit),
    SYS_DEF(SYS_WAIT4, "wait4", 4, sys_wait4),
    SYS_DEF(SYS_KILL, "kill", 2, sys_kill),
    SYS_DEF(SYS_UNAME, "uname", 1, sys_uname),
    SYS_DEF(SYS_FCNTL, "fcntl", 3, sys_fcntl),
    SYS_DEF(SYS_FTRUNCATE, "ftruncate", 2, sys_ftruncate),
    SYS_DEF(SYS_GETDENTS, "getdents", 3, sys_getdents),
    SYS_DEF(SYS_GETCWD, "getcwd", 2, sys_getcwd),
    SYS_DEF(SYS_CHDIR, "chdir", 1, sys_chdir),
    SYS_DEF(SYS_MKDIR, "mkdir", 2, sys_mkdir),
    SYS_DEF(SYS_RMDIR, "rmdir", 1, sys_rmdir),
    SYS_DEF(SYS_CREAT, "creat", 2, sys_creat),
    SYS_DEF(SYS_UNLINK, "unlink", 1, sys_unlink),
    SYS_DEF(SYS_SYMLINK, "symlink", 2, sys_symlink),
    SYS_DEF(SYS_READLINK, "readlink", 3, sys_readlink),
    SYS_DEF(SYS_RENAME, "rename", 2, sys_rename),
    SYS_DEF(SYS_GETTIMEOFDAY, "gettimeofday", 2, sys_gettimeofday),
    SYS_DEF(SYS_GETRLIMIT, "getrlimit", 2, sys_getrlimit),
    SYS_DEF(SYS_ARCH_PRCTL, "arch_prctl", 2, sys_arch_prctl),
    SYS_DEF(SYS_GETTID, "gettid", 0, sys_gettid),
    SYS_DEF(SYS_GETPPID, "getppid", 0, sys_getppid),
    SYS_DEF(SYS_FUTEX, "futex", 6, sys_futex),
    SYS_DEF(
        SYS_SCHED_GETAFFINITY, "sched_getaffinity", 3, sys_sched_getaffinity
    ),
    SYS_DEF(SYS_GETDENTS64, "getdents64", 3, sys_getdents64),
    SYS_DEF(SYS_CLOCK_GETTIME, "clock_gettime", 2, sys_clock_gettime),
    SYS_DEF(SYS_EXIT_GROUP, "exit_group", 1, sys_exit_group),
    SYS_DEF(SYS_OPENAT, "openat", 4, sys_openat),
    SYS_DEF(SYS_NEWFSTATAT, "newfstatat", 4, sys_newfstatat),
    SYS_DEF(SYS_FACCESSAT, "faccessat", 4, sys_faccessat),
    SYS_DEF(SYS_PIPE2, "pipe2", 2, sys_pipe2),
    SYS_DEF(SYS_SIGALTSTACK, "sigaltstack", 2, sys_sigaltstack),
    SYS_DEF(SYS_SET_TID_ADDRESS, "set_tid_address", 1, sys_set_tid_address),
    SYS_DEF(SYS_PRLIMIT64, "prlimit64", 4, sys_prlimit64),
    SYS_DEF(SYS_GETUID, "getuid", 0, sys_getuid),
    SYS_DEF(SYS_GETGID, "getgid", 0, sys_getgid),
    SYS_DEF(SYS_GETEUID, "geteuid", 0, sys_geteuid),
    SYS_DEF(SYS_GETEGID, "getegid", 0, sys_getegid),
    SYS_DEF(SYS_TKILL, "tkill", 2, sys_tkill),
    SYS_DEF(SYS_TGKILL, "tgkill", 3, sys_tgkill),
    SYS_DEF(SYS_ALCOR_FB_INFO, "alcor_fb_info", 1, sys_alcor_fb_info),
    SYS_DEF(SYS_ALCOR_FB_MMAP, "alcor_fb_mmap", 2, sys_alcor_fb_mmap),
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
