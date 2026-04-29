/**
 * @file user/shell/ast.h
 * @brief vega abstract syntax tree.
 *
 * AST node kinds are introduced as the parser learns them. Phase 1 only
 * defines AST_CMD; later phases extend the union.
 */

#ifndef VEGA_AST_H
#define VEGA_AST_H

typedef enum {
  AST_CMD,
} ast_kind_t;

/**
 * @brief Simple command node: argv[0..argc-1] with a NULL terminator at
 * argv[argc]. Each string is heap-allocated and owned by the node.
 */
typedef struct ast_node
{
  ast_kind_t kind;
  union {
    struct {
      char **argv;
      int    argc;
      int    cap;
    } cmd;
  } u;
} ast_t;

/** @brief Allocate a new AST_CMD node with empty argv. */
ast_t *ast_new_cmd(void);

/**
 * @brief Append @p arg (taking ownership of the heap pointer) to a CMD node's
 * argv. Grows the array as needed and maintains the trailing NULL.
 *
 * @return 0 on success, -1 on allocation failure (in which case the caller
 *         must free @p arg).
 */
int ast_cmd_push_arg(ast_t *n, char *arg);

/** @brief Free an AST node and everything it owns. NULL-safe. */
void ast_free(ast_t *n);

#endif /* VEGA_AST_H */
