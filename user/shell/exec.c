/**
 * @file user/shell/exec.c
 * @brief Walks vega AST nodes and runs them.
 *
 * AST_CMD: resolve via /bin or /usr/bin (or absolute path), fork+execve+wait,
 * applying redirections in the child. AST_AND/OR/SEQ short-circuit the obvious
 * way. AST_PIPE forks N children plumbed by N-1 pipes; pipeline status is the
 * last stage's. Builtins run in the shell process when standalone (so cd
 * mutates parent state); builtins in a pipeline run in a forked subshell.
 */

#include "exec.h"
#include "expand.h"
#include "shell.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_EXEC_PATH    256
#define MAX_PIPE_STAGES  16

/* Set up a here-string: pipe, write @p text + trailing newline into it, close
 * the write end, dup2 the read end onto fd 0. Caps at the pipe buffer size
 * (~4KB on this kernel); larger here-strings would need a temp-file path. */
static int apply_herestring(const char *text)
{
  int pipefd[2];
  if(pipe(pipefd) < 0)
    return -1;

  size_t len = sh_strlen(text);
  if(len > 0)
    write(pipefd[1], text, len);
  write(pipefd[1], "\n", 1);
  close(pipefd[1]);

  if(dup2(pipefd[0], 0) < 0) {
    close(pipefd[0]);
    return -1;
  }
  close(pipefd[0]);
  return 0;
}

/* Open @p target with flags appropriate for the redir kind, then dup2 onto the
 * canonical fd (0 for IN, 1 for OUT/APPEND). Returns 0 on success, -1 on any
 * error — caller is expected to bail out. */
static int apply_one_redir(const redir_t *r)
{
  if(r->kind == REDIR_HERESTRING)
    return apply_herestring(r->target);

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

/* Expand $-syntax in @p cmd's argv and redir targets in place. Existing heap
 * strings are freed and replaced with the expansion. Returns 0 on success,
 * -1 on allocation failure (the cmd is left in a usable but partially
 * expanded state). */
static int expand_cmd(ast_t *cmd)
{
  for(int i = 0; i < cmd->u.cmd.argc; i++) {
    char *expanded = expand_word(cmd->u.cmd.argv[i]);
    if(!expanded)
      return -1;
    free(cmd->u.cmd.argv[i]);
    cmd->u.cmd.argv[i] = expanded;
  }
  for(redir_t *r = cmd->u.cmd.redirs; r; r = r->next) {
    char *expanded = expand_word(r->target);
    if(!expanded)
      return -1;
    free(r->target);
    r->target = expanded;
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
  if(n->u.cmd.argc == 0)
    return 0;

  if(expand_cmd(n) < 0)
    return 1;

  int      argc   = n->u.cmd.argc;
  char   **argv   = n->u.cmd.argv;
  redir_t *redirs = n->u.cmd.redirs;

  if(argv[0][0] == '\0')
    return 0; /* expansion produced empty command name */

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

/* Run @p stage (an AST_CMD) in the current child after the pipeline plumbing
 * has set up stdin/stdout. Applies the stage's own redirs (which override the
 * pipeline plumbing per-fd, matching bash), then either runs a builtin and
 * exits, or execve's. Never returns. */
static void exec_stage_in_child(ast_t *stage) __attribute__((noreturn));
static void exec_stage_in_child(ast_t *stage)
{
  int    argc = stage->u.cmd.argc;
  char **argv = stage->u.cmd.argv;
  if(argc == 0)
    _exit(0);

  if(apply_redirs(stage->u.cmd.redirs) < 0)
    _exit(1);

  if(is_builtin(argv[0])) {
    int rc = run_builtin(argc, argv);
    _exit(rc);
  }

  char path[MAX_EXEC_PATH];
  if(!resolve_path(argv[0], path)) {
    sh_puts(argv[0]);
    sh_puts(": command not found\n");
    _exit(127);
  }
  execve(path, argv, NULL);
  _exit(127);
}

static int exec_pipeline(ast_t *n)
{
  int     N      = n->u.pipeline.n;
  ast_t **stages = n->u.pipeline.stages;

  if(N > MAX_PIPE_STAGES) {
    sh_puts("vega: pipeline too long\n");
    return 1;
  }

  /* Expand each stage's argv/redirs in the parent so children fork with the
   * fully-resolved command. */
  for(int i = 0; i < N; i++) {
    if(expand_cmd(stages[i]) < 0)
      return 1;
  }

  int pipes[MAX_PIPE_STAGES - 1][2];
  for(int i = 0; i < N - 1; i++) {
    if(pipe(pipes[i]) < 0) {
      for(int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      sh_puts("vega: pipe failed\n");
      return 1;
    }
  }

  int pids[MAX_PIPE_STAGES];
  for(int i = 0; i < N; i++) {
    pids[i] = fork();
    if(pids[i] < 0) {
      for(int j = 0; j < N - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      sh_puts("vega: fork failed\n");
      return 1;
    }
    if(pids[i] == 0) {
      if(i > 0)
        dup2(pipes[i - 1][0], 0);
      if(i < N - 1)
        dup2(pipes[i][1], 1);
      for(int j = 0; j < N - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      exec_stage_in_child(stages[i]);
    }
  }

  for(int i = 0; i < N - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  int last_status = 0;
  for(int i = 0; i < N; i++) {
    int status = 0;
    waitpid(pids[i], &status, 0);
    if(i == N - 1)
      last_status = (status >> 8) & 0xff;
  }
  return last_status;
}

int vega_exec(ast_t *node)
{
  if(!node)
    return 0;

  int status;
  switch(node->kind) {
    case AST_CMD:
      status = exec_cmd(node);
      break;
    case AST_AND: {
      int s = vega_exec(node->u.binop.left);
      status = (s == 0) ? vega_exec(node->u.binop.right) : s;
      break;
    }
    case AST_OR: {
      int s = vega_exec(node->u.binop.left);
      status = (s != 0) ? vega_exec(node->u.binop.right) : s;
      break;
    }
    case AST_SEQ:
      vega_exec(node->u.binop.left);
      status = vega_exec(node->u.binop.right);
      break;
    case AST_PIPE:
      status = exec_pipeline(node);
      break;
    case AST_IF: {
      int cond = vega_exec(node->u.if_.cond);
      if(cond == 0) {
        status = vega_exec(node->u.if_.then_branch);
      } else if(node->u.if_.else_branch) {
        status = vega_exec(node->u.if_.else_branch);
      } else {
        status = 0;
      }
      break;
    }
    default:
      status = 0;
  }
  expand_set_status(status);
  return status;
}
