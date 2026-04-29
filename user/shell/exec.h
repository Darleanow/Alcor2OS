/**
 * @file user/shell/exec.h
 * @brief vega AST executor.
 */

#ifndef VEGA_EXEC_H
#define VEGA_EXEC_H

#include "ast.h"

/**
 * @brief Execute @p node and return its exit status (0 on success,
 * non-zero on failure, 127 if a command was not found).
 */
int vega_exec(ast_t *node);

#endif /* VEGA_EXEC_H */
