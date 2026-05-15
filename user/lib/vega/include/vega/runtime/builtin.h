/**
 * @file vega/runtime/builtin.h
 * @brief Builtin lookup, libvega-internal. The implementation lives in
 * src/runtime/builtin.c; exec.c calls is_builtin / run_builtin during command
 * dispatch.
 */

#ifndef VEGA_RUNTIME_BUILTIN_H
#define VEGA_RUNTIME_BUILTIN_H

/** True if @p cmd matches a known builtin name. */
int is_builtin(const char *cmd);

/** Execute @p argv as a builtin (POSIX-style: argv[0] is the name,
 *  argv[argc] is NULL). Returns the builtin's exit status. */
int run_builtin(int argc, char *const argv[]);

#endif /* VEGA_RUNTIME_BUILTIN_H */
