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
  AST_AND,  /* left && right, short-circuit on non-zero status */
  AST_OR,   /* left || right, short-circuit on zero status */
  AST_SEQ,  /* left ; right, status is right's */
  AST_PIPE, /* a | b | ... ; status is last stage's */
} ast_kind_t;

typedef enum {
  REDIR_OUT,        /* >   file — truncate */
  REDIR_APPEND,     /* >>  file — append */
  REDIR_IN,         /* <   file */
  REDIR_HERESTRING, /* <<< text — feed text on stdin (target is the content,
                                  not a path) */
} redir_kind_t;

/**
 * @brief Linked list of file redirections attached to an AST_CMD. The target
 * string is heap-owned. Multiple redirs targeting the same fd apply in order
 * (last one wins, matching bash).
 */
typedef struct redir
{
  redir_kind_t  kind;
  char         *target;
  struct redir *next;
} redir_t;

/**
 * @brief AST node. CMD nodes own a heap argv and an optional redir list;
 * binary nodes own their two children. The union is grown by later phases
 * (pipes, control flow).
 */
typedef struct ast_node
{
  ast_kind_t kind;
  union {
    struct {
      char    **argv;
      int       argc;
      int       cap;
      redir_t  *redirs;
    } cmd;
    struct {
      struct ast_node *left;
      struct ast_node *right;
    } binop;
    struct {
      struct ast_node **stages; /* each stage owns its node */
      int               n;
      int               cap;
    } pipeline;
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
 * @brief Append a redirection to a CMD node. @p target is taken by ownership.
 *
 * @return 0 on success, -1 on allocation failure (caller must free @p target).
 */
int ast_cmd_add_redir(ast_t *n, redir_kind_t kind, char *target);

/**
 * @brief Allocate a binary operator node (AND, OR or SEQ) taking ownership of
 * @p left and @p right. On allocation failure both children are freed and
 * NULL is returned.
 */
ast_t *ast_new_binop(ast_kind_t kind, ast_t *left, ast_t *right);

/** @brief Allocate an empty AST_PIPE node. */
ast_t *ast_new_pipeline(void);

/**
 * @brief Append @p stage to a pipeline (taking ownership). Grows as needed.
 *
 * @return 0 on success, -1 on allocation failure (caller must free @p stage).
 */
int ast_pipeline_push(ast_t *n, ast_t *stage);

/** @brief Free an AST node and everything it owns. NULL-safe. */
void ast_free(ast_t *n);

#endif /* VEGA_AST_H */
