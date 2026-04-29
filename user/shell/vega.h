/**
 * @file user/shell/vega.h
 * @brief vega - Alcor2's shell scripting language. Public entry points.
 *
 * vega is a from-scratch bash-flavoured shell language with cleaner syntax:
 * brace-delimited control flow, brace string interpolation, and a single
 * canonical form for command substitution. This header declares the entry
 * points used by the REPL in main.c.
 */

#ifndef VEGA_H
#define VEGA_H

#define VEGA_VERSION "0.1.0"

/**
 * @brief Parse and execute one line of input.
 *
 * The line buffer is mutated in place. Output, exit-status tracking and
 * builtin dispatch are handled internally.
 *
 * @param line Null-terminated input line (modified during parsing).
 */
void vega_run(const char *line);

#endif /* VEGA_H */
