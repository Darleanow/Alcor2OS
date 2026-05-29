/**
 * @file user/sdk/vega/alcor_tty.c
 * @brief Definitions for the Alcor2 TTY / signal-routing user wrappers.
 *
 * Declarations live in @c <alcor2/tty_user.h>. The definitions sit
 * with vega today because vega is the only caller (registers the
 * foreground PID around child fork/wait). If a second user appears, lift
 * the file out to @c user/lib/ so binaries can link it independently of
 * libvega.
 */

#include <alcor2/tty_user.h>
#include <sys/syscall.h>
#include <unistd.h>

long alcor_set_fg_pid(long pid)
{
  return syscall(SYS_ALCOR_SET_FG_PID, pid);
}
