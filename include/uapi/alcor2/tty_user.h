/**
 * @file include/uapi/alcor2/tty_user.h
 * @brief Userland helpers for the Alcor2 TTY / signal-routing syscalls.
 *
 * Thin inline wrappers around the @c SYS_ALCOR_* syscalls that don't fit
 * the standard POSIX surface — currently just foreground-PID registration
 * for the canonical-mode keyboard path.
 */

#ifndef ALCOR2_ALCOR_TTY_USER_H
#define ALCOR2_ALCOR_TTY_USER_H

#include <alcor2/syscall.h> /* SYS_ALCOR_SET_FG_PID — single source of truth. */

/**
 * @brief Tell the kernel which PID is the current TTY foreground "job".
 *
 * Called by the shell after @c fork() of a child it intends to wait on,
 * so canonical-mode VINTR (Ctrl+C) delivers @c SIGINT to that child
 * instead of the shell. Reset to the shell's own PID (or 0) after the
 * child is reaped.
 *
 * Definition lives in @c user/sdk/vega/alcor_tty.c.
 *
 * @param pid  PID to register, or 0 to clear.
 * @return 0 on success, negative errno on failure.
 */
long alcor_set_fg_pid(long pid);

#endif
