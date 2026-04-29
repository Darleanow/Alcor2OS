/**
 * @file user/shell/ast.c
 * @brief AST node allocation and teardown.
 */

#include "ast.h"
#include <stdlib.h>

#define INITIAL_ARGV_CAP 4

ast_t *ast_new_cmd(void)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n)
    return NULL;
  n->kind        = AST_CMD;
  n->u.cmd.argv  = (char **)malloc(sizeof(char *) * INITIAL_ARGV_CAP);
  if(!n->u.cmd.argv) {
    free(n);
    return NULL;
  }
  n->u.cmd.argv[0] = NULL;
  n->u.cmd.argc    = 0;
  n->u.cmd.cap     = INITIAL_ARGV_CAP;
  return n;
}

int ast_cmd_push_arg(ast_t *n, char *arg)
{
  if(n->u.cmd.argc + 1 >= n->u.cmd.cap) {
    int    new_cap = n->u.cmd.cap * 2;
    char **new_arr = (char **)realloc(n->u.cmd.argv, sizeof(char *) * new_cap);
    if(!new_arr)
      return -1;
    n->u.cmd.argv = new_arr;
    n->u.cmd.cap  = new_cap;
  }
  n->u.cmd.argv[n->u.cmd.argc++] = arg;
  n->u.cmd.argv[n->u.cmd.argc]   = NULL;
  return 0;
}

ast_t *ast_new_binop(ast_kind_t kind, ast_t *left, ast_t *right)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    ast_free(left);
    ast_free(right);
    return NULL;
  }
  n->kind          = kind;
  n->u.binop.left  = left;
  n->u.binop.right = right;
  return n;
}

void ast_free(ast_t *n)
{
  if(!n)
    return;
  switch(n->kind) {
    case AST_CMD:
      for(int i = 0; i < n->u.cmd.argc; i++)
        free(n->u.cmd.argv[i]);
      free(n->u.cmd.argv);
      break;
    case AST_AND:
    case AST_OR:
    case AST_SEQ:
      ast_free(n->u.binop.left);
      ast_free(n->u.binop.right);
      break;
  }
  free(n);
}
