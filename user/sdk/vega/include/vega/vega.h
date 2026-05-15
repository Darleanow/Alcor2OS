/**
 * @file vega/vega.h
 * @brief vega — Alcor2's shell scripting language. Public entry points.
 *
 * vega is a from-scratch bash-flavoured shell language with cleaner syntax:
 * brace-delimited control flow, brace string interpolation, and a single
 * canonical form for command substitution. Embedders include this header
 * plus <vega/host.h> (the sh_* interface they must implement).
 */

#ifndef VEGA_H
#define VEGA_H

#define VEGA_VERSION "0.1.0"

/**
 * @brief Parse and execute one line of vega input.
 *
 * @param line Null-terminated input.
 * @return Exit status of the last command, 0 if the line parsed empty.
 */
int vega_run(const char *line);

/**
 * @brief Assign @p value to vega variable @p name.
 *
 * Variables created via this function are visible to subsequent vega_run
 * calls through $-syntax and {}-interpolation. Hosts use this to implement
 * a `let` builtin or to seed environment-derived variables.
 *
 * @return 0 on success, -1 on allocation failure or table overflow.
 */
int vega_setvar(const char *name, const char *value);

#endif /* VEGA_H */
