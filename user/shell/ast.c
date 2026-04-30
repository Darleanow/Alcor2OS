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
  n->u.cmd.redirs  = NULL;
  return n;
}

int ast_cmd_add_redir(ast_t *n, redir_kind_t kind, char *target)
{
  redir_t *r = (redir_t *)malloc(sizeof(*r));
  if(!r)
    return -1;
  r->kind   = kind;
  r->target = target;
  r->next   = NULL;

  /* append to keep apply order matching source order (last-wins per fd) */
  redir_t **tail = &n->u.cmd.redirs;
  while(*tail)
    tail = &(*tail)->next;
  *tail = r;
  return 0;
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

#define INITIAL_PIPELINE_CAP 2

ast_t *ast_new_pipeline(void)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n)
    return NULL;
  n->kind = AST_PIPE;
  n->u.pipeline.stages =
      (ast_t **)malloc(sizeof(ast_t *) * INITIAL_PIPELINE_CAP);
  if(!n->u.pipeline.stages) {
    free(n);
    return NULL;
  }
  n->u.pipeline.n   = 0;
  n->u.pipeline.cap = INITIAL_PIPELINE_CAP;
  return n;
}

int ast_pipeline_push(ast_t *n, ast_t *stage)
{
  if(n->u.pipeline.n >= n->u.pipeline.cap) {
    int     new_cap = n->u.pipeline.cap * 2;
    ast_t **new_arr =
        (ast_t **)realloc(n->u.pipeline.stages, sizeof(ast_t *) * new_cap);
    if(!new_arr)
      return -1;
    n->u.pipeline.stages = new_arr;
    n->u.pipeline.cap    = new_cap;
  }
  n->u.pipeline.stages[n->u.pipeline.n++] = stage;
  return 0;
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
      for(redir_t *r = n->u.cmd.redirs; r;) {
        redir_t *next = r->next;
        free(r->target);
        free(r);
        r = next;
      }
      break;
    case AST_AND:
    case AST_OR:
    case AST_SEQ:
      ast_free(n->u.binop.left);
      ast_free(n->u.binop.right);
      break;
    case AST_PIPE:
      for(int i = 0; i < n->u.pipeline.n; i++)
        ast_free(n->u.pipeline.stages[i]);
      free(n->u.pipeline.stages);
      break;
  }
  free(n);
}
