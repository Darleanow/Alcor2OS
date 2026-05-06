/**
 * @file src/kernel/sys/signal.c
 * @brief POSIX signal system calls and delivery mechanism.
 *
 * Implements rt_sigaction, rt_sigprocmask, rt_sigreturn, kill, and the
 * signal delivery hook called from the syscall return path.
 *
 * Signal delivery flow:
 *   1. A signal is marked pending via proc_signal() (e.g. from kill or
 *      proc_exit SIGCHLD).
 *   2. On each syscall return, proc_check_signals() inspects pending &
 *      ~masked for the current process.
 *   3. If a deliverable signal exists and has a user handler, a
 *      sig_ucontext_t is pushed below the red zone on the user stack and
 *      the syscall frame is redirected to the handler.
 *   4. The handler runs, then calls sa_restorer which executes
 *      syscall(SYS_RT_SIGRETURN).
 *   5. sys_rt_sigreturn restores all registers from sig_ucontext_t and
 *      resumes the interrupted code.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/signal.h>
#include <alcor2/sys/syscall.h>
#include <alcor2/mm/vmm.h>


/* Helper: default signal disposition. */

/**
 * @brief Return true if the default action for a signal is to ignore it.
 */
static int sig_default_ignore(int signum)
{
  switch(signum) {
  case SIGCHLD:
  case SIGURG:
  case SIGWINCH:
  case SIGCONT:
    return 1;
  default:
    return 0;
  }
}

/* Syscall implementations. */

/**
 * @brief Register or query a signal action.
 *
 * Syscall 13: rt_sigaction(signum, act, oldact, sigsetsize)
 *
 * @param signum     Signal number.
 * @param act        New action (user pointer to k_sigaction_t, may be NULL).
 * @param oldact     Old action output (user pointer, may be NULL).
 * @param sigsetsize Size of signal set (must be 8).
 * @return 0 on success, negative errno on error.
 */
u64 sys_rt_sigaction(
    u64 signum, u64 act, u64 oldact, u64 sigsetsize, u64 a5, u64 a6
)
{
  (void)a5;
  (void)a6;

  if(sigsetsize != 8)
    return (u64)-EINVAL;
  if(signum == 0 || signum >= NSIG)
    return (u64)-EINVAL;
  if(signum == SIGKILL || signum == SIGSTOP)
    return (u64)-EINVAL;

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ESRCH;

  if(oldact) {
    if(!vmm_is_user_range((void *)oldact, sizeof(k_sigaction_t)))
      return (u64)-EFAULT;
    kmemcpy((void *)oldact, &p->sig_actions[signum], sizeof(k_sigaction_t));
  }

  if(act) {
    if(!vmm_is_user_range((void *)act, sizeof(k_sigaction_t)))
      return (u64)-EFAULT;
    kmemcpy(&p->sig_actions[signum], (const void *)act, sizeof(k_sigaction_t));
  }

  return 0;
}

/**
 * @brief Examine and change the blocked signal mask.
 *
 * Syscall 14: rt_sigprocmask(how, set, oldset, sigsetsize)
 *
 * @param how        SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK.
 * @param set        New mask (user pointer to u64, may be NULL).
 * @param oldset     Old mask output (user pointer, may be NULL).
 * @param sigsetsize Size of signal set (must be 8).
 * @return 0 on success, negative errno on error.
 */
u64 sys_rt_sigprocmask(
    u64 how, u64 set, u64 oldset, u64 sigsetsize, u64 a5, u64 a6
)
{
  (void)a5;
  (void)a6;

  if(sigsetsize != 8)
    return (u64)-EINVAL;

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ESRCH;

  if(oldset) {
    if(!vmm_is_user_range((void *)oldset, sizeof(u64)))
      return (u64)-EFAULT;
    *(u64 *)oldset = p->sig_mask;
  }

  if(set) {
    if(!vmm_is_user_range((void *)set, sizeof(u64)))
      return (u64)-EFAULT;

    u64 new_mask = *(u64 *)set;
    /* SIGKILL and SIGSTOP cannot be blocked */
    new_mask &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));

    switch((int)how) {
    case SIG_BLOCK:
      p->sig_mask |= new_mask;
      break;
    case SIG_UNBLOCK:
      p->sig_mask &= ~new_mask;
      break;
    case SIG_SETMASK:
      p->sig_mask = new_mask;
      break;
    default:
      return (u64)-EINVAL;
    }
  }

  return 0;
}

/**
 * @brief Return from a signal handler by restoring saved register context.
 *
 * Syscall 15: rt_sigreturn()
 *
 * On entry, frame->rsp is the user RSP at the time the sigreturn syscall
 * was issued, which points to the sig_ucontext_t pushed by
 * proc_check_signals when the signal was delivered.
 *
 * @return Original rax (syscall return value interrupted by signal).
 */
u64 sys_rt_sigreturn(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  syscall_frame_t *frame = syscall_get_current_frame();
  if(!frame)
    return (u64)-EINVAL;

  /* frame->rsp is user RSP at sigreturn call time = &sig_ucontext_t */
  sig_ucontext_t *ctx = (sig_ucontext_t *)frame->rsp;
  if(!vmm_is_user_range(ctx, sizeof(sig_ucontext_t)))
    return (u64)-EFAULT;

  /* Restore all general-purpose registers */
  frame->r15    = ctx->r15;
  frame->r14    = ctx->r14;
  frame->r13    = ctx->r13;
  frame->r12    = ctx->r12;
  frame->r11    = ctx->r11;
  frame->r10    = ctx->r10;
  frame->r9     = ctx->r9;
  frame->r8     = ctx->r8;
  frame->rbp    = ctx->rbp;
  frame->rdi    = ctx->rdi;
  frame->rsi    = ctx->rsi;
  frame->rdx    = ctx->rdx;
  frame->rcx    = ctx->rcx;
  frame->rbx    = ctx->rbx;
  frame->rax    = ctx->rax;
  frame->rip    = ctx->rip;
  frame->rflags = ctx->rflags;
  frame->rsp    = ctx->rsp;

  /* Restore signal mask */
  proc_t *p = proc_current();
  if(p)
    p->sig_mask = ctx->sig_mask;

  /* Return ctx->rax — syscall_dispatch stores it back into frame->rax */
  return ctx->rax;
}

/**
 * @brief Send a signal to a process.
 *
 * Syscall 62: kill(pid, sig)
 *
 * @param pid Target PID (-1 = all processes, not fully implemented).
 * @param sig Signal number (0 = existence check only).
 * @return 0 on success, negative errno on error.
 */
u64 sys_kill(u64 pid, u64 sig, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(sig >= NSIG)
    return (u64)-EINVAL;

  if((i64)pid == -1) {
    /* Broadcast: not fully implemented; succeed silently */
    return 0;
  }

  proc_t *target = proc_get((u64)pid);
  if(!target)
    return (u64)-ESRCH;

  if(sig == 0)
    return 0; /* Existence check */

  proc_signal((u64)pid, (int)sig);
  return 0;
}

/* tkill(tid, sig) — single-threaded kernel: tid maps to pid. */
u64 sys_tkill(u64 tid, u64 sig, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_kill(tid, sig, a3, a4, a5, a6);
}

/* tgkill(tgid, tid, sig) — same: route to sys_kill on the tgid. */
u64 sys_tgkill(u64 tgid, u64 tid, u64 sig, u64 a4, u64 a5, u64 a6)
{
  (void)tid;
  return sys_kill(tgid, sig, 0, a4, a5, a6);
}

/* sigaltstack(const stack_t *ss, stack_t *old_ss) — alternate signal stack.
 * No real altstack support: zero out old_ss if requested, ignore ss. */
u64 sys_sigaltstack(u64 ss, u64 old_ss, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)ss;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(old_ss) {
    /* stack_t = { void *ss_sp; int ss_flags; size_t ss_size; } = 24 bytes */
    u8 *p = (u8 *)old_ss;
    for(unsigned i = 0; i < 24; i++)
      p[i] = 0;
    /* mark SS_DISABLE = 2 in ss_flags */
    *(int *)(p + 8) = 2;
  }
  return 0;
}

/* Signal delivery helpers. */

/**
 * @brief Mark a signal as pending for a process and wake it if blocked.
 *
 * @param pid    Target process ID.
 * @param signum Signal number (1 … NSIG-1).
 */
void proc_signal(u64 pid, int signum)
{
  if(signum <= 0 || signum >= NSIG)
    return;

  proc_t *p = proc_get(pid);
  if(!p || p->state == PROC_STATE_FREE || p->state == PROC_STATE_ZOMBIE)
    return;

  p->sig_pending |= (1ULL << signum);

  /* Unblock a sleeping process so it can handle the signal */
  if(p->state == PROC_STATE_BLOCKED)
    p->state = PROC_STATE_READY;
}

/**
 * @brief Check for deliverable signals and set up a signal frame if needed.
 *
 * Called from syscall_entry (ASM) after syscall_dispatch returns,
 * before restoring user registers. May modify the syscall frame to
 * redirect execution to a signal handler.
 *
 * Delivery layout on user stack (grows down):
 * @code
 *   [new RSP]        → sa_restorer address  (return address for the handler)
 *   [new RSP + 8]    → sig_ucontext_t       (saved context for rt_sigreturn)
 * @endcode
 *
 * @param frame_ptr Pointer to the syscall_frame_t on the kernel stack.
 */
void proc_check_signals(void *frame_ptr)
{
  proc_t *p = proc_current();
  if(!p)
    return;

  /* Deliverable = pending & ~masked */
  u64 deliverable = p->sig_pending & ~p->sig_mask;
  if(!deliverable)
    return;

  /* Find the lowest-numbered pending signal */
  int signum = 0;
  for(int i = 1; i < NSIG; i++) {
    if(deliverable & (1ULL << i)) {
      signum = i;
      break;
    }
  }
  if(!signum)
    return;

  /* Clear it from pending */
  p->sig_pending &= ~(1ULL << signum);

  k_sigaction_t *act = &p->sig_actions[signum];

  /* SIG_IGN: silently discard */
  if(act->sa_handler == SIG_IGN)
    return;

  /* SIG_DFL: default action */
  if(act->sa_handler == SIG_DFL) {
    if(!sig_default_ignore(signum))
      proc_exit(-(i64)signum);
    return;
  }

  /* SIGKILL / SIGSTOP cannot be caught */
  if(signum == SIGKILL || signum == SIGSTOP)
    proc_exit(-(i64)signum);

  /* Deliver to user handler: build signal frame on user stack. */
  syscall_frame_t *frame = (syscall_frame_t *)frame_ptr;

  /*
   * Stack layout we construct (addresses decrease downward):
   *
   *   frame->rsp (original user RSP)
   *     − 128        red zone (ABI requirement)
   *     − sizeof(sig_ucontext_t)  aligned down to 16
   *     − 8          sa_restorer address  ← new RSP
   */
  u64 user_rsp = frame->rsp;
  user_rsp -= 128; /* skip System V x86-64 red zone */
  user_rsp -= sizeof(sig_ucontext_t);
  user_rsp &= ~0xFULL; /* 16-byte align */

  sig_ucontext_t *ctx = (sig_ucontext_t *)user_rsp;

  /* Need space for the context plus the 8-byte return address below it */
  if(!vmm_is_user_range((void *)(user_rsp - 8), sizeof(sig_ucontext_t) + 8))
    return;

  /* Save all current registers into the user-space context */
  ctx->r15      = frame->r15;
  ctx->r14      = frame->r14;
  ctx->r13      = frame->r13;
  ctx->r12      = frame->r12;
  ctx->r11      = frame->r11;
  ctx->r10      = frame->r10;
  ctx->r9       = frame->r9;
  ctx->r8       = frame->r8;
  ctx->rbp      = frame->rbp;
  ctx->rdi      = frame->rdi;
  ctx->rsi      = frame->rsi;
  ctx->rdx      = frame->rdx;
  ctx->rcx      = frame->rcx;
  ctx->rbx      = frame->rbx;
  ctx->rax      = frame->rax; /* already set to syscall return value */
  ctx->rip      = frame->rip;
  ctx->rflags   = frame->rflags;
  ctx->rsp      = frame->rsp;
  ctx->sig_mask = p->sig_mask;
  ctx->signum   = (u32)signum;
  ctx->_pad     = 0;

  /* Push sa_restorer as return address beneath the context */
  user_rsp -= 8;
  *(u64 *)user_rsp = act->sa_restorer;

  /* Update signal mask for handler execution */
  if(!(act->sa_flags & SA_NODEFER))
    p->sig_mask |= (1ULL << signum);
  p->sig_mask |= act->sa_mask;

  /* SA_RESETHAND: revert to SIG_DFL after one delivery */
  if(act->sa_flags & SA_RESETHAND)
    act->sa_handler = SIG_DFL;

  /* Redirect the syscall return to the signal handler */
  frame->rip    = act->sa_handler;   /* jump to handler */
  frame->rdi    = (u64)signum;       /* arg1: signal number */
  frame->rsi    = 0;                 /* arg2: siginfo_t* (NULL) */
  frame->rdx    = (u64)ctx;          /* arg3: ucontext_t* */
  frame->rsp    = user_rsp;          /* new user stack */
  frame->rflags = 0x202;             /* IF=1, reserved bit */
}
