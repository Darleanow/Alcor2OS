/**
 * @file vega/host.h
 * @brief libvega embedding interface.
 *
 * The kernel framebuffer console handles glyph rendering, ANSI parsing, and
 * blink — so libvega doesn't intercept output. The host only needs to provide
 * a handful of things the language itself can't decide:
 *
 *   - which builtin commands are available in this host (shell has cd/pwd/...,
 *     CLI has none).
 *
 * Everything else (stdout, filesystem queries, etc.) libvega does through
 * plain musl.
 */

#ifndef VEGA_HOST_H
#define VEGA_HOST_H

#include <stdbool.h>
#include <stddef.h>

/** Cap on path-string buffers used inside libvega (cwd, redir targets, …). */
#define MAX_PATH 256

/**
 * @brief Operations libvega calls back into the host for. All fields are
 * required; hosts with no extra builtins return false from @c is_builtin.
 */
typedef struct vega_host_ops
{
  /** True if @p name names a builtin the host implements. */
  bool (*is_builtin)(const char *name);

  /** Execute a host builtin. argv[0] is the name, argv[argc] is NULL.
   *  Returns the builtin's exit status. */
  int (*run_builtin)(int argc, char *const argv[]);
} vega_host_ops_t;

/**
 * @brief Register the host operations table. Must be called once before any
 * vega_run() / vega_setvar() call. The pointer is held; @p ops must outlive
 * the lifetime of any vega call.
 */
void vega_init(const vega_host_ops_t *ops);

#endif /* VEGA_HOST_H */
