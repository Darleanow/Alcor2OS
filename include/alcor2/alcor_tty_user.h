/**
 * @file include/alcor2/alcor_tty_user.h
 * @brief Userland helpers for the Alcor2 TTY / signal-routing syscalls.
 *
 * Thin inline wrappers around the @c SYS_ALCOR_* syscalls that don't fit
 * the standard POSIX surface — currently just foreground-PID registration
 * for the canonical-mode keyboard path.
 */

#ifndef ALCOR2_ALCOR_TTY_USER_H
#define ALCOR2_ALCOR_TTY_USER_H

#include <sys/syscall.h>
#include <unistd.h>

/** @brief Custom syscall number — must match the kernel's syscall.h. */
#ifndef SYS_ALCOR_SET_FG_PID
  #define SYS_ALCOR_SET_FG_PID 497
#endif

/**
 * @brief Tell the kernel which PID is the current TTY foreground "job".
 *
 * Called by the shell after @c fork() of a child it intends to wait on,
 * so canonical-mode VINTR (Ctrl+C) delivers @c SIGINT to that child
 * instead of the shell. Reset to the shell's own PID (or 0) after the
 * child is reaped.
 *
 * @param pid  PID to register, or 0 to clear.
 * @return 0 on success, negative errno on failure.
 */
static inline long alcor_set_fg_pid(long pid)
{
  return syscall(SYS_ALCOR_SET_FG_PID, pid);
}

#endif
