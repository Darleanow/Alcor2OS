/**
 * @file src/kernel/sys/sys_dispatch.c
 * @brief Syscall table and dispatch from the ASM entry stub.
 *
 * @par Behaviour
 * Sets `g_current_syscall_frame` for the duration of the handler (signals /
 * rt_sigreturn), invokes the handler with RDI…R9 per Linux x86_64 ABI, then
 * calls `sched_check_resched()` before returning the value in RAX.
 *
 * Unknown syscall number or NULL table slot: return `(u64)-ENOSYS`.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>
#include <alcor2/sys/internal.h>

static syscall_frame_t *g_current_syscall_frame = NULL;

syscall_frame_t        *syscall_get_current_frame(void)
{
  return g_current_syscall_frame;
}

static const syscall_fn_t g_syscall_table[SYS_MAX] = {
    [SYS_READ]              = sys_read,
    [SYS_WRITE]             = sys_write,
    [SYS_OPEN]              = sys_open,
    [SYS_CLOSE]             = sys_close,
    [SYS_STAT]              = sys_stat,
    [SYS_FSTAT]             = sys_fstat,
    [SYS_LSTAT]             = sys_lstat,
    [SYS_LSEEK]             = sys_lseek,
    [SYS_MMAP]              = sys_mmap,
    [SYS_MPROTECT]          = sys_mprotect,
    [SYS_MUNMAP]            = sys_munmap,
    [SYS_BRK]               = sys_brk,
    [SYS_RT_SIGACTION]      = sys_rt_sigaction,
    [SYS_RT_SIGPROCMASK]    = sys_rt_sigprocmask,
    [SYS_RT_SIGRETURN]      = sys_rt_sigreturn,
    [SYS_IOCTL]             = sys_ioctl,
    [SYS_PREAD64]           = sys_pread64,
    [SYS_PWRITE64]          = sys_pwrite64,
    [SYS_READV]             = sys_readv,
    [SYS_WRITEV]            = sys_writev,
    [SYS_ACCESS]            = sys_access,
    [SYS_FACCESSAT]         = sys_faccessat,
    [SYS_PIPE]              = sys_pipe,
    [SYS_SELECT]            = sys_select,
    [SYS_PIPE2]             = sys_pipe2,
    [SYS_SIGALTSTACK]       = sys_sigaltstack,
    [SYS_SCHED_YIELD]       = sys_sched_yield,
    [SYS_SCHED_GETAFFINITY] = sys_sched_getaffinity,
    [SYS_DUP]               = sys_dup,
    [SYS_DUP2]              = sys_dup2,
    [SYS_NANOSLEEP]         = sys_nanosleep,
    [SYS_GETPID]            = sys_getpid,
    [SYS_CLONE]             = sys_clone,
    [SYS_FORK]              = sys_fork,
    [SYS_EXECVE]            = sys_execve,
    [SYS_EXIT]              = sys_exit,
    [SYS_WAIT4]             = sys_wait4,
    [SYS_KILL]              = sys_kill,
    [SYS_TKILL]             = sys_tkill,
    [SYS_TGKILL]            = sys_tgkill,
    [SYS_UNAME]             = sys_uname,
    [SYS_FCNTL]             = sys_fcntl,
    [SYS_FTRUNCATE]         = sys_ftruncate,
    [SYS_GETDENTS]          = sys_getdents,
    [SYS_GETCWD]            = sys_getcwd,
    [SYS_CHDIR]             = sys_chdir,
    [SYS_RENAME]            = sys_rename,
    [SYS_MKDIR]             = sys_mkdir,
    [SYS_RMDIR]             = sys_rmdir,
    [SYS_CREAT]             = sys_creat,
    [SYS_SYMLINK]           = sys_symlink,
    [SYS_UNLINK]            = sys_unlink,
    [SYS_READLINK]          = sys_readlink,
    [SYS_GETTIMEOFDAY]      = sys_gettimeofday,
    [SYS_GETUID]            = sys_getuid,
    [SYS_GETGID]            = sys_getgid,
    [SYS_GETEUID]           = sys_geteuid,
    [SYS_GETEGID]           = sys_getegid,
    [SYS_GETPPID]           = sys_getppid,
    [SYS_ARCH_PRCTL]        = sys_arch_prctl,
    [SYS_GETTID]            = sys_gettid,
    [SYS_FUTEX]             = sys_futex,
    [SYS_SET_TID_ADDRESS]   = sys_set_tid_address,
    [SYS_CLOCK_GETTIME]     = sys_clock_gettime,
    [SYS_EXIT_GROUP]        = sys_exit,
    [SYS_GETDENTS64]        = sys_getdents64,
    [SYS_OPENAT]            = sys_openat,
    [SYS_NEWFSTATAT]        = sys_newfstatat,
    [SYS_GETRLIMIT]         = sys_getrlimit,
    [SYS_PRLIMIT64]         = sys_prlimit64,
    [SYS_ALCOR_FB_INFO]     = sys_alcor_fb_info,
    [SYS_ALCOR_FB_MMAP]     = sys_alcor_fb_mmap,
};

u64 syscall_dispatch(syscall_frame_t *frame)
{
  u64 num = frame->rax;
  if(num >= SYS_MAX || g_syscall_table[num] == NULL) {
    console_printf(
        "[SYS?%d pid=%d]\n", (int)num,
        proc_current() ? (int)proc_current()->pid : -1
    );
    return (u64)-ENOSYS;
  }

  g_current_syscall_frame = frame;
  u64 result              = g_syscall_table[num](
      frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9
  );
  g_current_syscall_frame = NULL;

  sched_check_resched();

  return result;
}
