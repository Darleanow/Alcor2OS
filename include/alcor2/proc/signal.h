/**
 * @file include/alcor2/proc/signal.h
 * @brief POSIX signal handling infrastructure (Linux x86_64 compatible).
 *
 * Implements a signal subsystem sufficient to run
 * programs that rely on signal delivery (for Clang/LLVM).
 */

#ifndef ALCOR2_SIGNAL_H
#define ALCOR2_SIGNAL_H

#include <alcor2/types.h>

/** @name Signal numbers (Linux x86_64 compatible)
 * @{ */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31
#define NSIG      64
/** @} */

/** @name Special handler values
 * @{ */
#define SIG_DFL ((u64)0)
#define SIG_IGN ((u64)1)
/** @} */

/** @name sigaction sa_flags
 *
 * The full Linux x86_64 set is exposed so musl's @c rt_sigaction wire layout
 * is preserved across the syscall boundary. Today the delivery path
 * (@c proc_check_signals) only consults @c SA_NODEFER and @c SA_RESETHAND;
 * the others are accepted into @c k_sigaction_t but otherwise inert.
 * @{ */
#define SA_NOCLDSTOP 0x00000001u /* inert */
#define SA_NOCLDWAIT 0x00000002u /* inert */
#define SA_SIGINFO   0x00000004u /* inert */
#define SA_RESTORER  0x04000000u /* inert */
#define SA_RESTART   0x10000000u /* inert */
#define SA_NODEFER   0x40000000u /* honored — see proc_check_signals */
#define SA_RESETHAND 0x80000000u /* honored — see proc_check_signals */
/** @} */

/** @name rt_sigprocmask 'how' values
 * @{ */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
/** @} */

/**
 * @brief Kernel-side sigaction (Linux x86_64 ABI compatible).
 *
 * Matches the layout used by musl's rt_sigaction syscall interface.
 */
typedef struct
{
  u64 sa_handler;  /**< Handler address, SIG_DFL (0), or SIG_IGN (1) */
  u64 sa_flags;    /**< SA_* flags */
  u64 sa_restorer; /**< Restorer trampoline — calls rt_sigreturn */
  u64 sa_mask;     /**< Additional signals to block during handler */
} k_sigaction_t;

/**
 * @brief User-space register context saved on the user stack during delivery.
 *
 * proc_check_signals pushes this below the red zone before redirecting
 * execution to the handler. rt_sigreturn reads it back to resume execution.
 */
typedef struct
{
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
  u64 rip;
  u64 rflags;
  u64 rsp;
  u64 sig_mask; /**< Saved signal mask, restored by rt_sigreturn */
  u32 signum;
  u32 _pad;
} sig_ucontext_t;

/**
 * @brief Check for deliverable signals and set up signal frame if needed.
 *
 * Called from syscall_entry (ASM) after syscall_dispatch returns.
 * May redirect the syscall frame to a signal handler by modifying
 * frame->rip, frame->rsp, and frame->rdi.
 *
 * @param frame Pointer to the syscall_frame_t on the kernel stack.
 */
void proc_check_signals(void *frame);

/**
 * @brief Asynchronously deliver a signal to a process.
 *
 * Sets the signal bit in the target process's pending mask and
 * wakes it if it is blocked.
 *
 * @param pid    Target process ID.
 * @param signum Signal number (1 … NSIG-1).
 */
void proc_signal(u64 pid, int signum);

#endif /* ALCOR2_SIGNAL_H */
