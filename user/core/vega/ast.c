/**
 * @file core/vega/ast.c
 * @brief AST node allocation and teardown.
 */

#include <stdlib.h>
#include <vega/ast.h>

#define INITIAL_ARGV_CAP 4

ast_t *ast_new_cmd(void)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n)
    return NULL;
  n->kind       = AST_CMD;
  n->u.cmd.argv = (char **)malloc(sizeof(char *) * INITIAL_ARGV_CAP);
  if(!n->u.cmd.argv) {
    free(n);
    return NULL;
  }
  n->u.cmd.argv[0]   = NULL;
  n->u.cmd.argc      = 0;
  n->u.cmd.cap       = INITIAL_ARGV_CAP;
  n->u.cmd.redirs    = NULL;
  n->u.cmd.fail_fast = 0;
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

ast_t *ast_new_if(ast_t *cond, ast_t *then_branch, ast_t *else_branch)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    ast_free(cond);
    ast_free(then_branch);
    ast_free(else_branch);
    return NULL;
  }
  n->kind              = AST_IF;
  n->u.if_.cond        = cond;
  n->u.if_.then_branch = then_branch;
  n->u.if_.else_branch = else_branch;
  return n;
}

ast_t *ast_new_while(ast_t *cond, ast_t *body)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    ast_free(cond);
    ast_free(body);
    return NULL;
  }
  n->kind          = AST_WHILE;
  n->u.while_.cond = cond;
  n->u.while_.body = body;
  return n;
}

#define INITIAL_FOR_CAP 4

ast_t *ast_new_for(char *name)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    free(name);
    return NULL;
  }
  n->kind         = AST_FOR;
  n->u.for_.name  = name;
  n->u.for_.words = (char **)malloc(sizeof(char *) * INITIAL_FOR_CAP);
  if(!n->u.for_.words) {
    free(name);
    free(n);
    return NULL;
  }
  n->u.for_.nwords = 0;
  n->u.for_.cap    = INITIAL_FOR_CAP;
  n->u.for_.body   = NULL;
  return n;
}

int ast_for_push_word(ast_t *n, char *word)
{
  if(n->u.for_.nwords >= n->u.for_.cap) {
    int    new_cap = n->u.for_.cap * 2;
    char **new_arr =
        (char **)realloc(n->u.for_.words, sizeof(char *) * new_cap);
    if(!new_arr)
      return -1;
    n->u.for_.words = new_arr;
    n->u.for_.cap   = new_cap;
  }
  n->u.for_.words[n->u.for_.nwords++] = word;
  return 0;
}

void ast_for_set_body(ast_t *n, ast_t *body)
{
  n->u.for_.body = body;
}

ast_t *ast_new_fn(char *name, char **arg_names, int n_args, ast_t *body)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    free(name);
    if(arg_names) {
      for(int i = 0; i < n_args; i++)
        free(arg_names[i]);
      free(arg_names);
    }
    ast_free(body);
    return NULL;
  }
  n->kind           = AST_FN;
  n->u.fn.name      = name;
  n->u.fn.arg_names = arg_names;
  n->u.fn.n_args    = n_args;
  n->u.fn.body      = body;
  return n;
}

ast_t *ast_new_let(char *name, char *value)
{
  ast_t *n = (ast_t *)malloc(sizeof(*n));
  if(!n) {
    free(name);
    free(value);
    return NULL;
  }
  n->kind         = AST_LET;
  n->u.let_.name  = name;
  n->u.let_.value = value;
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
  case AST_IF:
    ast_free(n->u.if_.cond);
    ast_free(n->u.if_.then_branch);
    ast_free(n->u.if_.else_branch);
    break;
  case AST_WHILE:
    ast_free(n->u.while_.cond);
    ast_free(n->u.while_.body);
    break;
  case AST_FOR:
    free(n->u.for_.name);
    for(int i = 0; i < n->u.for_.nwords; i++)
      free(n->u.for_.words[i]);
    free(n->u.for_.words);
    ast_free(n->u.for_.body);
    break;
  case AST_FN:
    /* Fields may have been NULL'd out when the function was registered
     * (the table steals ownership). Each free() is NULL-safe. */
    free(n->u.fn.name);
    if(n->u.fn.arg_names) {
      for(int i = 0; i < n->u.fn.n_args; i++)
        free(n->u.fn.arg_names[i]);
      free(n->u.fn.arg_names);
    }
    ast_free(n->u.fn.body);
    break;
  case AST_LET:
    free(n->u.let_.name);
    free(n->u.let_.value);
    break;
  }
  free(n);
}
