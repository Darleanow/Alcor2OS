/**
 * @file user/shell/exec.c
 * @brief Walks vega AST nodes and runs them.
 *
 * Phase 1 only handles AST_CMD: builtin lookup, otherwise external resolution
 * via /bin and /usr/bin.
 */

#include "exec.h"
#include "shell.h"

#define MAX_EXEC_PATH 256

/* Until the kernel split (Phase 4) sys_execve prepends the program path to
 * argv internally, so userland must pass argv with argv[0] stripped. */
static int run_external(char *const argv[])
{
  if(argv[0][0] == '/') {
    int ret = sh_exec(argv[0], &argv[1]);
    return (ret < 0) ? -1 : ret;
  }

  static const char *const dirs[] = { "/bin/", "/usr/bin/", NULL };
  char                     path[MAX_EXEC_PATH];

  for(int i = 0; dirs[i]; i++) {
    char       *p      = path;
    const char *prefix = dirs[i];
    while(*prefix && p < path + MAX_EXEC_PATH - 1)
      *p++ = *prefix++;
    const char *c = argv[0];
    while(*c && p < path + MAX_EXEC_PATH - 1)
      *p++ = *c++;
    *p      = '\0';
    int ret = sh_exec(path, &argv[1]);
    if(ret >= 0)
      return ret;
  }
  return -1;
}

static int exec_cmd(ast_t *n)
{
  int    argc = n->u.cmd.argc;
  char **argv = n->u.cmd.argv;

  if(argc == 0)
    return 0;

  if(is_builtin(argv[0]))
    return run_builtin(argc, argv);

  int ret = run_external(argv);
  if(ret < 0) {
    sh_puts(argv[0]);
    sh_puts(": command not found\n");
    return 127;
  }
  return ret;
}

int vega_exec(ast_t *node)
{
  if(!node)
    return 0;
  switch(node->kind) {
    case AST_CMD:
      return exec_cmd(node);
    case AST_AND: {
      int s = vega_exec(node->u.binop.left);
      if(s == 0)
        return vega_exec(node->u.binop.right);
      return s;
    }
    case AST_OR: {
      int s = vega_exec(node->u.binop.left);
      if(s != 0)
        return vega_exec(node->u.binop.right);
      return s;
    }
    case AST_SEQ:
      vega_exec(node->u.binop.left);
      return vega_exec(node->u.binop.right);
  }
  return 0;
}
