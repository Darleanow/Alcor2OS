/**
 * @file include/uapi/alcor2/syscall.h
 * @brief UAPI: syscall numbers shared between kernel and userland.
 *
 * Single source of truth. Linux-compatible numbers for the standard POSIX
 * surface (so unmodified musl reaches them) plus Alcor-specific extensions
 * in the 497–499 slot. Kernel-private dispatcher decls
 * (@c syscall_init, @c syscall_dispatch, …) stay in
 * @c <alcor2/sys/syscall.h>.
 */

#ifndef ALCOR2_UAPI_SYSCALL_H
#define ALCOR2_UAPI_SYSCALL_H

/** @name Linux-compatible syscall numbers (musl uses these names too).
 * @{ */
#define SYS_READ              0
#define SYS_WRITE             1
#define SYS_OPEN              2
#define SYS_CLOSE             3
#define SYS_STAT              4
#define SYS_FSTAT             5
#define SYS_LSTAT             6
#define SYS_POLL              7
#define SYS_LSEEK             8
#define SYS_MMAP              9
#define SYS_MPROTECT          10
#define SYS_MUNMAP            11
#define SYS_BRK               12
#define SYS_RT_SIGACTION      13
#define SYS_RT_SIGPROCMASK    14
#define SYS_RT_SIGRETURN      15
#define SYS_IOCTL             16
#define SYS_PREAD64           17
#define SYS_PWRITE64          18
#define SYS_READV             19
#define SYS_WRITEV            20
#define SYS_ACCESS            21
#define SYS_PIPE              22
#define SYS_SELECT            23
#define SYS_SCHED_YIELD       24
#define SYS_DUP               32
#define SYS_DUP2              33
#define SYS_NANOSLEEP         35
#define SYS_GETPID            39
#define SYS_FACCESSAT         48
#define SYS_CLONE             56
#define SYS_FORK              57
#define SYS_EXECVE            59
#define SYS_EXIT              60
#define SYS_WAIT4             61
#define SYS_KILL              62
#define SYS_UNAME             63
#define SYS_FCNTL             72
#define SYS_FTRUNCATE         77
#define SYS_GETDENTS          78
#define SYS_GETCWD            79
#define SYS_CHDIR             80
#define SYS_RENAME            82
#define SYS_MKDIR             83
#define SYS_RMDIR             84
#define SYS_CREAT             85
#define SYS_UNLINK            87
#define SYS_SYMLINK           88
#define SYS_READLINK          89
#define SYS_GETTIMEOFDAY      96
#define SYS_GETRLIMIT         97
#define SYS_GETUID            102
#define SYS_GETGID            104
#define SYS_GETEUID           107
#define SYS_GETEGID           108
#define SYS_GETPPID           110
#define SYS_SIGALTSTACK       131
#define SYS_ARCH_PRCTL        158
#define SYS_MOUNT             165
#define SYS_UMOUNT2           166
#define SYS_GETTID            186
#define SYS_TKILL             200
#define SYS_FUTEX             202
#define SYS_SCHED_GETAFFINITY 204
#define SYS_GETDENTS64        217
#define SYS_SET_TID_ADDRESS   218
#define SYS_CLOCK_GETTIME     228
#define SYS_EXIT_GROUP        231
#define SYS_TGKILL            234
#define SYS_OPENAT            257
#define SYS_NEWFSTATAT        262
#define SYS_PIPE2             293
#define SYS_PRLIMIT64         302
/** @} */

/** @name Alcor-specific syscalls (497–499).
 * @{ */
#define SYS_ALCOR_SET_FG_PID  497 /**< Register TTY foreground PID (shell only). */
#define SYS_ALCOR_FB_INFO     498 /**< User FB geometry (@ref alcor_fb_info_t). */
#define SYS_ALCOR_FB_MMAP     499 /**< Map linear framebuffer (RW, shared). */
/** @} */

/** @brief One past the last valid syscall number. */
#define SYS_MAX               512

#endif /* ALCOR2_UAPI_SYSCALL_H */
