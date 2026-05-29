/**
 * @file user/crt/alcor_tty.c
 * @brief Definitions for the Alcor2 TTY / signal-routing user wrappers.
 *
 * Declarations live in @c <alcor2/alcor_tty_user.h>; the definitions are
 * here so callers link against one symbol rather than each translation
 * unit inlining its own copy.
 */

#include <alcor2/alcor_tty_user.h>
#include <sys/syscall.h>
#include <unistd.h>

long alcor_set_fg_pid(long pid)
{
  return syscall(SYS_ALCOR_SET_FG_PID, pid);
}
