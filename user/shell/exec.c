/**
 * @file user/shell/exec.c
 * @brief Walks vega AST nodes and runs them.
 *
 * AST_CMD: resolve via /bin or /usr/bin (or absolute path), fork+execve+wait,
 * applying redirections in the child. AST_AND/OR/SEQ short-circuit the obvious
 * way. Builtins run in the shell process — for builtins with redirs, we save
 * fd 0/1 with dup, apply, run, then restore.
 */

#include "exec.h"
#include "shell.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_EXEC_PATH 256

/* Open @p target with flags appropriate for the redir kind, then dup2 onto the
 * canonical fd (0 for IN, 1 for OUT/APPEND). Returns 0 on success, -1 on any
 * error — caller is expected to bail out. */
static int apply_one_redir(const redir_t *r)
{
  int flags = 0;
  int dest_fd;
  switch(r->kind) {
    case REDIR_OUT:    flags = O_WRONLY | O_CREAT | O_TRUNC;  dest_fd = 1; break;
    case REDIR_APPEND: flags = O_WRONLY | O_CREAT | O_APPEND; dest_fd = 1; break;
    case REDIR_IN:     flags = O_RDONLY;                      dest_fd = 0; break;
    default:           return -1;
  }

  int fd = open(r->target, flags, 0644);
  if(fd < 0) {
    sh_puts("vega: cannot open ");
    sh_puts(r->target);
    sh_puts("\n");
    return -1;
  }
  if(fd != dest_fd) {
    if(dup2(fd, dest_fd) < 0) {
      close(fd);
      return -1;
    }
    close(fd);
  }
  return 0;
}

static int apply_redirs(const redir_t *list)
{
  for(const redir_t *r = list; r; r = r->next) {
    if(apply_one_redir(r) < 0)
      return -1;
  }
  return 0;
}

/* Look up @p name in the search path, copying the result into @p out_path
 * (size MAX_EXEC_PATH). Returns 1 if found, 0 otherwise. */
static int resolve_path(const char *name, char *out_path)
{
  if(name[0] == '/') {
    char       *p = out_path;
    const char *c = name;
    while(*c && p < out_path + MAX_EXEC_PATH - 1)
      *p++ = *c++;
    *p = '\0';
    return 1;
  }

  static const char *const dirs[] = { "/bin/", "/usr/bin/", NULL };
  for(int i = 0; dirs[i]; i++) {
    char       *p      = out_path;
    const char *prefix = dirs[i];
    while(*prefix && p < out_path + MAX_EXEC_PATH - 1)
      *p++ = *prefix++;
    const char *c = name;
    while(*c && p < out_path + MAX_EXEC_PATH - 1)
      *p++ = *c++;
    *p = '\0';

    struct stat st;
    if(sh_stat(out_path, &st) == 0)
      return 1;
  }
  return 0;
}

static int run_external(char *const argv[], const redir_t *redirs)
{
  char path[MAX_EXEC_PATH];
  if(!resolve_path(argv[0], path))
    return -1;

  int pid = fork();
  if(pid < 0)
    return -1;
  if(pid == 0) {
    if(apply_redirs(redirs) < 0)
      _exit(1);
    execve(path, argv, NULL);
    _exit(127);
  }
  int status = 0;
  if(waitpid(pid, &status, 0) < 0)
    return -1;
  return (status >> 8) & 0xff;
}

/* Run a builtin under @p redirs. We try to save fds 0/1 with dup; if dup
 * returns -EBADF that just means the shell is using the kernel's stdio
 * fallback (no per-process fd installed), so there's nothing to restore —
 * after the builtin we just close fd 0/1 again to drop back to the fallback. */
static int run_builtin_redirected(int argc, char *const argv[],
                                  const redir_t *redirs)
{
  if(!redirs)
    return run_builtin(argc, argv);

  int saved_in  = dup(0); /* may be -1 (fallback) */
  int saved_out = dup(1);

  int rc = apply_redirs(redirs);
  if(rc == 0)
    rc = run_builtin(argc, argv);

  if(saved_in >= 0) {
    dup2(saved_in, 0);
    close(saved_in);
  } else {
    close(0); /* drop the redir back to fallback */
  }
  if(saved_out >= 0) {
    dup2(saved_out, 1);
    close(saved_out);
  } else {
    close(1);
  }
  return rc;
}

static int exec_cmd(ast_t *n)
{
  int      argc   = n->u.cmd.argc;
  char   **argv   = n->u.cmd.argv;
  redir_t *redirs = n->u.cmd.redirs;

  if(argc == 0)
    return 0;

  if(is_builtin(argv[0]))
    return run_builtin_redirected(argc, argv, redirs);

  int ret = run_external(argv, redirs);
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
