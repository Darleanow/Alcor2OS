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
  AST_AND, /* left && right, short-circuit on non-zero status */
  AST_OR,  /* left || right, short-circuit on zero status */
  AST_SEQ, /* left ; right, status is right's */
} ast_kind_t;

/**
 * @brief AST node. CMD nodes own a heap argv; binary nodes own their two
 * children. The union is grown by later phases (pipes, redirections, control
 * flow).
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
    struct {
      struct ast_node *left;
      struct ast_node *right;
    } binop;
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

/**
 * @brief Allocate a binary operator node (AND, OR or SEQ) taking ownership of
 * @p left and @p right. On allocation failure both children are freed and
 * NULL is returned.
 */
ast_t *ast_new_binop(ast_kind_t kind, ast_t *left, ast_t *right);

/** @brief Free an AST node and everything it owns. NULL-safe. */
void ast_free(ast_t *n);

#endif /* VEGA_AST_H */
