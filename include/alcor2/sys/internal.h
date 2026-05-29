/**
 * @file include/alcor2/sys/internal.h
 * @brief Kernel-only declarations for the syscall implementation layer.
 *
 * Not a user-facing API. Centralizes prototypes expected by `syscall_dispatch`
 * and the `sys_*.c`, `pipe.c`, and `signal.c` modules.
 *
 * @par Handler contract
 * Each `sys_*` takes six `u64` arguments in Linux x86_64 order: RDI, RSI, RDX,
 * R10, R8, R9 (the dispatcher reads the syscall frame and passes them through
 * unchanged). Return value is `u64`; on error use `(u64)-errno` with codes from
 * `errno.h` (e.g. `(u64)-EINVAL`).
 *
 * The `SYSCALL_DECL(name)` macro pins this signature so it cannot drift.
 *
 * @par Unimplemented numbers
 * Numbers not present in the table in `sys_dispatch.c` yield `-ENOSYS` (see
 * implementation).
 */

#ifndef ALCOR2_SYS_INTERNAL_H
#define ALCOR2_SYS_INTERNAL_H

#include <alcor2/fs/pipe.h>
#include <alcor2/sys/syscall.h>

typedef u64 (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

/** @brief Descriptor for a single syscall mapping.
 *
 * Handler signature is invariant six u64-in / u64-out (System V order:
 * RDI, RSI, RDX, R10, R8, R9). No nargs field — every entry accepts
 * the full register window and ignores unused slots via (void)aN.
 */
typedef struct
{
  u64          num;     /**< Syscall number (RAX) */
  const char  *name;    /**< Symbolic name for tracing */
  syscall_fn_t handler; /**< Implementation function */
} sys_def_t;

#define SYSCALL_DECL(name) u64 name(u64, u64, u64, u64, u64, u64)

/* I/O */
SYSCALL_DECL(sys_read);
SYSCALL_DECL(sys_write);
SYSCALL_DECL(sys_lseek);
SYSCALL_DECL(sys_ioctl);
SYSCALL_DECL(sys_nanosleep);
SYSCALL_DECL(sys_readv);
SYSCALL_DECL(sys_writev);
SYSCALL_DECL(sys_select);
SYSCALL_DECL(sys_poll);

/* Memory */
SYSCALL_DECL(sys_mmap);
SYSCALL_DECL(sys_mprotect);
SYSCALL_DECL(sys_munmap);
SYSCALL_DECL(sys_brk);

/* Filesystem and paths */
SYSCALL_DECL(sys_open);
SYSCALL_DECL(sys_close);
SYSCALL_DECL(sys_stat);
SYSCALL_DECL(sys_fstat);
SYSCALL_DECL(sys_lstat);
SYSCALL_DECL(sys_access);
SYSCALL_DECL(sys_faccessat);
SYSCALL_DECL(sys_newfstatat);
SYSCALL_DECL(sys_dup);
SYSCALL_DECL(sys_dup2);
SYSCALL_DECL(sys_fcntl);
SYSCALL_DECL(sys_getdents);
SYSCALL_DECL(sys_getdents64);
SYSCALL_DECL(sys_getcwd);
SYSCALL_DECL(sys_chdir);
SYSCALL_DECL(sys_mkdir);
SYSCALL_DECL(sys_rmdir);
SYSCALL_DECL(sys_creat);
SYSCALL_DECL(sys_unlink);
SYSCALL_DECL(sys_rename);
SYSCALL_DECL(sys_ftruncate);
SYSCALL_DECL(sys_pread64);
SYSCALL_DECL(sys_pwrite64);
SYSCALL_DECL(sys_symlink);
SYSCALL_DECL(sys_openat);
SYSCALL_DECL(sys_readlink);

/* Process */
SYSCALL_DECL(sys_getpid);
SYSCALL_DECL(sys_gettid);
SYSCALL_DECL(sys_getppid);
SYSCALL_DECL(sys_clone);
SYSCALL_DECL(sys_fork);
SYSCALL_DECL(sys_execve);
SYSCALL_DECL(sys_exit);
SYSCALL_DECL(sys_wait4);
SYSCALL_DECL(sys_uname);
SYSCALL_DECL(sys_getuid);
SYSCALL_DECL(sys_getgid);
SYSCALL_DECL(sys_geteuid);
SYSCALL_DECL(sys_getegid);
SYSCALL_DECL(sys_set_tid_address);

/* Misc */
SYSCALL_DECL(sys_gettimeofday);
SYSCALL_DECL(sys_futex);
SYSCALL_DECL(sys_clock_gettime);
SYSCALL_DECL(sys_sched_yield);
SYSCALL_DECL(sys_sched_getaffinity);
SYSCALL_DECL(sys_getrlimit);
SYSCALL_DECL(sys_prlimit64);

/* Signals and arch (Linux ABI) */
SYSCALL_DECL(sys_rt_sigaction);
SYSCALL_DECL(sys_rt_sigprocmask);
SYSCALL_DECL(sys_rt_sigreturn);
SYSCALL_DECL(sys_sigaltstack);
SYSCALL_DECL(sys_kill);
SYSCALL_DECL(sys_tkill);
SYSCALL_DECL(sys_tgkill);
SYSCALL_DECL(sys_arch_prctl);

/* Pipe */
SYSCALL_DECL(sys_pipe);
SYSCALL_DECL(sys_pipe2);
SYSCALL_DECL(sys_exit_group);

/* Userspace FB / compositors */
SYSCALL_DECL(sys_alcor_fb_info);
SYSCALL_DECL(sys_alcor_fb_mmap);

/* TTY foreground PID registration (shell uses this around fork/wait). */
SYSCALL_DECL(sys_alcor_set_fg_pid);

/* Pipe interface: see <alcor2/fs/pipe.h> (included above). */

#undef SYSCALL_DECL

#endif
