/**
 * @file include/alcor2/syscall.h
 * @brief x86_64 syscall interface and definitions.
 *
 * Syscalls use the SYSCALL/SYSRET instructions with System V AMD64 calling convention.
 * Syscall number in RAX, arguments in RDI, RSI, RDX, R10, R8, R9.
 * Return value in RAX. RCX and R11 are clobbered by SYSCALL.
 */

#ifndef ALCOR2_SYSCALL_H
#define ALCOR2_SYSCALL_H

#include <alcor2/types.h>

/** @name Syscall numbers (Linux x86_64 compatible subset)
 * @{ */
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_LSTAT           6
#define SYS_POLL            7
#define SYS_LSEEK           8
#define SYS_MMAP            9
#define SYS_MPROTECT        10
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_IOCTL           16
#define SYS_ACCESS          21
#define SYS_PIPE            22
#define SYS_SCHED_YIELD     24
#define SYS_DUP             32
#define SYS_DUP2            33
#define SYS_NANOSLEEP       35
#define SYS_GETPID          39
#define SYS_CLONE           56
#define SYS_FORK            57
#define SYS_EXECVE          59
#define SYS_EXIT            60
#define SYS_WAIT4           61
#define SYS_KILL            62
#define SYS_UNAME           63
#define SYS_FCNTL           72
#define SYS_GETDENTS        78
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_CREAT           85
#define SYS_UNLINK          87
#define SYS_READLINK        89
#define SYS_GETTIMEOFDAY    96
#define SYS_GETUID          102
#define SYS_GETGID          104
#define SYS_GETEUID         107
#define SYS_GETEGID         108
#define SYS_GETPPID         110
#define SYS_ARCH_PRCTL      158
#define SYS_MOUNT           165
#define SYS_UMOUNT2         166
#define SYS_GETTID          186
#define SYS_FUTEX           202
#define SYS_SET_TID_ADDRESS 218
#define SYS_CLOCK_GETTIME   228
#define SYS_EXIT_GROUP      231
#define SYS_GETDENTS64      217
#define SYS_OPENAT          257
#define SYS_MAX             512
/** @} */

/**
 * @brief Saved registers on syscall entry.
 */
typedef struct
{
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx;
  u64 rax;
  u64 rip;
  u64 rflags;
  u64 rsp;
} syscall_frame_t;

/**
 * @brief Initialize syscall mechanism (set MSRs).
 */
void syscall_init(void);

/**
 * @brief Syscall dispatcher (called from ASM entry).
 * @param frame Saved syscall frame.
 * @return Syscall return value.
 */
u64 syscall_dispatch(syscall_frame_t *frame);

/** @name MSR definitions for SYSCALL/SYSRET
 * @{ */
#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define EFER_SCE   (1 << 0)
/** @} */

#endif
