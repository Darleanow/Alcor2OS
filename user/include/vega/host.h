/**
 * @file vega/host.h
 * @brief libvega embedding interface.
 *
 * libvega is a freestanding language runtime; it performs no syscalls itself.
 * Embedders (the interactive shell, the standalone vega CLI, future tools)
 * supply a vega_host_ops_t describing how to perform the few operations the
 * runtime needs, then call vega_init(&ops) once before any vega_run().
 *
 * Patterned after the embedding interfaces of SQLite, Lua, and similar
 * single-header libraries: one struct of function pointers, one
 * registration call, no implicit symbol coupling.
 */

#ifndef VEGA_HOST_H
#define VEGA_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

/** Cap on path-string buffers used inside libvega (cwd, redir targets, …). */
#define MAX_PATH 256

/**
 * @brief Operations libvega calls into for I/O, filesystem queries, fb_tty
 * coordination, and host-defined builtins. All fields are required (no NULL
 * — hosts that have no FB TTY return false from fb_tty_active and no-op the
 * lifecycle hooks; hosts with no extra builtins return false from is_builtin).
 */
typedef struct vega_host_ops
{
  /* --- I/O --- */

  /** Write a NUL-terminated string to host stdout. */
  void (*puts)(const char *s);

  /** Write @p len bytes to host stdout, bypassing any line-buffering. */
  void (*stdout_bytes)(const void *buf, size_t len);

  /* --- File descriptor primitives (used by exec.c for pipe relay) --- */

  /** Close @p fd. */
  int (*close)(int fd);

  /** Read up to @p len bytes from @p fd. */
  long (*read)(int fd, void *buf, size_t len);

  /** Stat @p path. Returns 0 on success, -1 on error. */
  int (*stat)(const char *path, struct stat *st);

  /* --- FB TTY hooks --- */

  /** True when the host is rendering through a framebuffer TTY. Affects
   *  whether exec.c sets up a pipe-relay around child stdout. */
  bool (*fb_tty_active)(void);

  /** Called in the child immediately after fork(), to detach FB TTY state. */
  void (*fb_tty_on_fork_child)(void);

  /** Called from the relay-pipe idle loop so the host can tick its blink. */
  void (*fb_tty_blink_tick)(void);

  /* --- Host builtins (cd, pwd, let, … on the shell; nothing on the CLI) --- */

  /** True if @p name names a builtin the host implements. */
  bool (*is_builtin)(const char *name);

  /** Execute a host builtin. Return value is the builtin's exit status. */
  int (*run_builtin)(int argc, char *const argv[]);
} vega_host_ops_t;

/**
 * @brief Register the host operations table. Must be called once before any
 * vega_run() / vega_setvar() call. The pointer is held; @p ops must outlive
 * the lifetime of any vega call.
 */
void vega_init(const vega_host_ops_t *ops);

#endif /* VEGA_HOST_H */
