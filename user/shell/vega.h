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
 * @param line Null-terminated input line.
 * @return The exit status of the last executed command, or 0 if the line
 *         parsed empty.
 */
int vega_run(const char *line);

#endif /* VEGA_H */
