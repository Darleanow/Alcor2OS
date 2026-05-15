/**
 * @file sdk/vega/builtin.h
 * @brief Language builtins (sdk-internal). exec.c calls is_builtin/run_builtin
 * for the small set of vega-language commands implemented in-process (cd, pwd,
 * let, exit). Shell-UX builtins (help, version, clear, kbd) live in
 * apps/shell — host registers them via sh_is_builtin / sh_run_builtin.
 */

#ifndef VEGA_SDK_BUILTIN_H
#define VEGA_SDK_BUILTIN_H

/** True if @p cmd matches a known language builtin name. */
int is_builtin(const char *cmd);

/** Execute @p argv as a language builtin (POSIX-style: argv[0] is the name,
 *  argv[argc] is NULL). Returns the builtin's exit status. */
int run_builtin(int argc, char *const argv[]);

#endif /* VEGA_SDK_BUILTIN_H */
