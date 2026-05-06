/**
 * @file include/alcor2/sys/internal.h
 * @brief Kernel-only declarations for the syscall implementation layer.
 *
 * Not a user-facing API. Centralizes prototypes expected by `syscall_dispatch` and the
 * `sys_*.c`, `pipe.c`, and `signal.c` modules.
 *
 * @par Handler contract
 * Each `sys_*` takes six `u64` arguments in Linux x86_64 order: RDI, RSI, RDX, R10, R8, R9
 * (the dispatcher reads the syscall frame and passes them through unchanged).
 * Return value is `u64`; on error use `(u64)-errno` with codes from `errno.h` (e.g. `(u64)-EINVAL`).
 *
 * The `SYSCALL_DECL(name)` macro pins this signature so it cannot drift.
 *
 * @par Unimplemented numbers
 * Numbers not present in the table in `sys_dispatch.c` yield `-ENOSYS` (see implementation).
 */

#ifndef ALCOR2_SYS_INTERNAL_H
#define ALCOR2_SYS_INTERNAL_H

#include <alcor2/sys/syscall.h>

typedef u64 (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

#define SYSCALL_DECL(name) u64 name(u64, u64, u64, u64, u64, u64)

/* I/O */
SYSCALL_DECL(sys_read);
SYSCALL_DECL(sys_write);
SYSCALL_DECL(sys_lseek);
SYSCALL_DECL(sys_ioctl);
SYSCALL_DECL(sys_nanosleep);
SYSCALL_DECL(sys_readv);
SYSCALL_DECL(sys_writev);

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

/**
 * @brief Read up to @p count bytes from the read end of a pipe object.
 * @return Bytes read (0 on EOF when write end is closed), or negative -errno.
 */
i64 pipe_read_obj(void *pipe, void *buf, u64 count);

/**
 * @brief Write up to @p count bytes to the write end of a pipe object.
 * @return Bytes written, or negative -errno.
 */
i64 pipe_write_obj(void *pipe, const void *buf, u64 count);

/**
 * @brief Allocate a fresh pipe object. Both ends start refcount=1; the caller
 * is expected to wrap it in two OFT entries via @c vfs_oft_alloc_pipe and
 * release one of them if any setup step fails.
 *
 * @return Opaque pipe pointer, or NULL on exhaustion.
 */
void *pipe_alloc_obj(void);

/**
 * @brief Called from the OFT release path when a pipe-end refcount hits zero.
 * Closes the corresponding end and frees the pipe object once both ends are
 * closed.
 *
 * @param kind  VFS_FD_PIPE_READ or VFS_FD_PIPE_WRITE.
 * @param pipe  Pipe pointer.
 */
void pipe_oft_release(i32 kind, void *pipe);

/**
 * @brief Bump refcounts on every open pipe end. Called by proc_fork so the
 * child can close its inherited fds without affecting the parent.
 */
void pipe_dup_for_fork(void);

/** @brief Mark a forked child as a holder of every pipe end the parent has open. */
void pipe_assign_to_proc(u64 child_pid, u64 parent_pid);

/** @brief Release every pipe end held by an exiting process; wakes blocked peers. */
void pipe_close_for_exit(u64 pid);

#undef SYSCALL_DECL

#endif
