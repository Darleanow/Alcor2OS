/**
 * @file sdk/vega/exec.c
 * @brief Walks vega AST nodes and runs them.
 *
 * AST_CMD: resolve via /bin or /usr/bin, absolute path, or relative path
 * containing '/' (e.g. ./a.out per POSIX); fork+execve+wait,
 * applying redirections in the child. AST_AND/OR/SEQ short-circuit the obvious
 * way. AST_PIPE forks N children plumbed by N-1 pipes; pipeline status is the
 * last stage's. Builtins run in the shell process when standalone (so cd
 * mutates parent state); builtins in a pipeline run in a forked subshell.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vega/host.h>
#include <vega/internal/exec.h>
#include <vega/internal/expand.h>
#include <vega/internal/fntab.h>
#include <vega/internal/host.h>
#include <vega/vega.h>

/* musl exposes this; must not pass NULL to execve — breaks getenv, setenv,
 * ncurses terminfo lookup, etc. (undefined environ → faults like CR2 ~0x45). */
extern char **environ;

#define MAX_EXEC_PATH   256
#define MAX_PIPE_STAGES 16

/** musl/clang treat argv[0] like /proc/self/exe — must be the resolved path.
 *  @return 0 on success, -1 if strdup fails (caller should _exit in the child).
 */
static int child_argv0_to_resolved_path(char **argv, const char *path)
{
  if(!argv || !argv[0] || !path)
    return -1;
  char *copy = strdup(path);
  if(!copy)
    return -1;
  free(argv[0]);
  argv[0] = copy;
  return 0;
}

/* Set up a here-string / heredoc: pipe, write @p text into it, close the
 * write end, dup2 the read end onto fd 0. Caps at the pipe buffer size
 * (~4KB on this kernel); larger payloads would need a temp-file path.
 * For here-strings (`<<<`) bash adds an implicit trailing newline; heredocs
 * already include the newline of their last body line. */
static int apply_pipe_input(const char *text, int append_newline)
{
  int pipefd[2];
  if(pipe(pipefd) < 0)
    return -1;

  size_t len = strlen(text);
  if(len > 0)
    write(pipefd[1], text, len);
  if(append_newline)
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
 * error — caller is expected to bail out. The redir's target is expanded here
 * (rather than once up-front) so that loop-bodies see fresh values per
 * iteration. */
static int apply_one_redir(const redir_t *r)
{
  char *target = expand_word(r->target);
  if(!target)
    return -1;

  if(r->kind == REDIR_HERESTRING || r->kind == REDIR_HEREDOC) {
    int rc = apply_pipe_input(target, r->kind == REDIR_HERESTRING);
    free(target);
    return rc;
  }

  int flags = 0;
  int dest_fd;
  switch(r->kind) {
  case REDIR_OUT:
    flags   = O_WRONLY | O_CREAT | O_TRUNC;
    dest_fd = 1;
    break;
  case REDIR_APPEND:
    flags   = O_WRONLY | O_CREAT | O_APPEND;
    dest_fd = 1;
    break;
  case REDIR_IN:
    flags   = O_RDONLY;
    dest_fd = 0;
    break;
  default:
    free(target);
    return -1;
  }

  int fd = open(target, flags, 0644);
  if(fd < 0) {
    (void)write(
        STDOUT_FILENO, ("vega: cannot open "), strlen(("vega: cannot open "))
    );
    (void)write(STDOUT_FILENO, (target), strlen((target)));
    (void)write(STDOUT_FILENO, ("\n"), strlen(("\n")));
    free(target);
    return -1;
  }
  free(target);
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

/* Build a fresh, NULL-terminated argv with each word expanded. Returns a
 * heap-allocated array of heap-allocated strings; caller frees via
 * free_expanded_argv. NULL on allocation failure. The AST is left untouched
 * so loop bodies (re-executed AST nodes) see fresh expansions each call. */
static char **build_expanded_argv(const ast_t *cmd)
{
  int    argc = cmd->u.cmd.argc;
  char **out  = (char **)malloc(sizeof(char *) * (argc + 1));
  if(!out)
    return NULL;
  for(int i = 0; i < argc; i++) {
    out[i] = expand_word(cmd->u.cmd.argv[i]);
    if(!out[i]) {
      for(int j = 0; j < i; j++)
        free(out[j]);
      free(out);
      return NULL;
    }
  }
  out[argc] = NULL;
  return out;
}

static void free_expanded_argv(char **argv, int argc)
{
  if(!argv)
    return;
  for(int i = 0; i < argc; i++)
    free(argv[i]);
  free(argv);
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

  /* POSIX: if the command name contains '/', search PATH is skipped — path is
   * relative to cwd (e.g. ./a.out, bin/foo). */
  if(strchr(name, '/')) {
    char       *p = out_path;
    const char *c = name;
    while(*c && p < out_path + MAX_EXEC_PATH - 1)
      *p++ = *c++;
    *p = '\0';
    struct stat st;
    if(stat(out_path, &st) == 0 && S_ISREG(st.st_mode))
      return 1;
    return 0;
  }

  static const char *const dirs[] = {"/bin/", "/usr/bin/", NULL};
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
    if(stat(out_path, &st) == 0)
      return 1;
  }
  return 0;
}

static int run_external(char **argv, const redir_t *redirs)
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
    if(child_argv0_to_resolved_path(argv, path) < 0)
      _exit(127);
    execve(path, argv, environ);
    _exit(127);
  }
  int status = 0;
  if(waitpid(pid, &status, 0) < 0)
    return -1;
  return (status >> 8) & 0xff;
}

/* Run an in-process callee (language builtin or host builtin) under @p redirs.
 * Saves fds 0/1 with dup; if dup returns -EBADF that just means the shell is
 * using the kernel's stdio fallback (no per-process fd installed), so there's
 * nothing to restore — after the call we close fd 0/1 again to drop back to
 * the fallback. */
typedef int (*builtin_fn_t)(int argc, char *const argv[]);

static int  run_in_process_redirected(
     builtin_fn_t fn, int argc, char *const argv[], const redir_t *redirs
 )
{
  if(!redirs)
    return fn(argc, argv);

  int saved_in  = dup(0); /* may be -1 (fallback) */
  int saved_out = dup(1);

  int rc = apply_redirs(redirs);
  if(rc == 0)
    rc = fn(argc, argv);

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

/* Bind positional args (argv[1..]) into the function's named parameters,
 * then exec the body. Missing args bind to "". Variables are global today,
 * so this clobbers any like-named outer var (no local scope). */
static int call_function(const fn_entry_t *fn, int argc, char *const argv[])
{
  for(int i = 0; i < fn->n_args; i++) {
    const char *val = (i + 1 < argc) ? argv[i + 1] : "";
    vega_setvar(fn->arg_names[i], val);
  }
  return vega_exec(fn->body);
}

/* Run a function under @p redirs. Mirrors run_builtin_redirected: dup-save
 * fds 0/1, apply redirs, run body, restore. */
static int call_function_redirected(
    const fn_entry_t *fn, int argc, char *const argv[], const redir_t *redirs
)
{
  if(!redirs)
    return call_function(fn, argc, argv);

  int saved_in  = dup(0);
  int saved_out = dup(1);

  int rc = apply_redirs(redirs);
  if(rc == 0)
    rc = call_function(fn, argc, argv);

  if(saved_in >= 0) {
    dup2(saved_in, 0);
    close(saved_in);
  } else {
    close(0);
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

  int      argc   = n->u.cmd.argc;
  char   **argv   = build_expanded_argv(n);
  redir_t *redirs = n->u.cmd.redirs;
  if(!argv)
    return 1;

  int ret;
  if(argv[0][0] == '\0') {
    ret = 0; /* expansion produced empty command name */
  } else {
    const fn_entry_t *fn = fntab_get(argv[0]);
    if(fn) {
      ret = call_function_redirected(fn, argc, argv, redirs);
    } else if(vega_host->is_builtin(argv[0])) {
      ret =
          run_in_process_redirected(vega_host->run_builtin, argc, argv, redirs);
    } else {
      ret = run_external(argv, redirs);
      if(ret < 0) {
        (void)write(STDOUT_FILENO, (argv[0]), strlen((argv[0])));
        (void)write(
            STDOUT_FILENO, (": command not found\n"),
            strlen((": command not found\n"))
        );
        ret = 127;
      }
    }
  }

  /* Postfix `!` sugar: if this cmd was marked fail-fast and exited non-zero,
   * tear down the shell with that status. Pipelines aren't covered: the
   * stage runs in a forked child and can only _exit itself, not the
   * parent. */
  int   fail_fast    = n->u.cmd.fail_fast;
  char *name_for_msg = NULL;
  if(fail_fast && ret != 0) {
    /* Copy name out before freeing argv so the message survives. */
    int len = 0;
    while(argv[0][len])
      len++;
    name_for_msg = (char *)malloc((size_t)len + 1);
    if(name_for_msg) {
      for(int i = 0; i <= len; i++)
        name_for_msg[i] = argv[0][i];
    }
  }

  free_expanded_argv(argv, argc);

  if(fail_fast && ret != 0) {
    (void)write(STDOUT_FILENO, ("vega: '"), strlen(("vega: '")));
    (void)write(
        STDOUT_FILENO, (name_for_msg ? name_for_msg : "?"),
        strlen((name_for_msg ? name_for_msg : "?"))
    );
    (void)write(STDOUT_FILENO, ("!' failed\n"), strlen(("!' failed\n")));
    free(name_for_msg);
    exit(ret);
  }
  return ret;
}

/* Run @p stage in the current child after the pipeline plumbing has set up
 * stdin/stdout. Compound nodes (if/while/for/fn) are exec'd via vega_exec
 * and the child exits with their status — the registration/state changes
 * stay confined to the child, but most pipeline stages are simple commands
 * anyway. AST_CMD: expand argv inside the child (so the AST is never mutated
 * and parent-side var changes between stages would be visible — though
 * pipelines are forked simultaneously today), apply stage redirs (which
 * override the pipeline plumbing per-fd, matching bash), then either run a
 * function/builtin and exit, or execve. Never returns. */
static void exec_stage_in_child(ast_t *stage) __attribute__((noreturn));
static void exec_stage_in_child(ast_t *stage)
{
  if(stage->kind != AST_CMD) {
    int rc = vega_exec(stage);
    _exit(rc);
  }

  int argc = stage->u.cmd.argc;
  if(argc == 0)
    _exit(0);

  char **argv = build_expanded_argv(stage);
  if(!argv)
    _exit(1);

  if(apply_redirs(stage->u.cmd.redirs) < 0)
    _exit(1);

  const fn_entry_t *fn = fntab_get(argv[0]);
  if(fn) {
    int rc = call_function(fn, argc, argv);
    _exit(rc);
  }

  if(vega_host->is_builtin(argv[0])) {
    int rc = vega_host->run_builtin(argc, argv);
    _exit(rc);
  }

  char path[MAX_EXEC_PATH];
  if(!resolve_path(argv[0], path)) {
    (void)write(STDOUT_FILENO, (argv[0]), strlen((argv[0])));
    (void)write(
        STDOUT_FILENO, (": command not found\n"),
        strlen((": command not found\n"))
    );
    _exit(127);
  }
  if(child_argv0_to_resolved_path(argv, path) < 0)
    _exit(127);
  execve(path, argv, environ);
  _exit(127);
}

static int exec_pipeline(ast_t *n)
{
  int     N      = n->u.pipeline.n;
  ast_t **stages = n->u.pipeline.stages;

  if(N > MAX_PIPE_STAGES) {
    (void)write(
        STDOUT_FILENO, ("vega: pipeline too long\n"),
        strlen(("vega: pipeline too long\n"))
    );
    return 1;
  }

  /* Expansion happens inside each child via exec_stage_in_child to avoid
   * mutating the shared AST (loop bodies re-execute the same nodes). The
   * last stage inherits fd 1 from the shell, which lands directly in the
   * kernel fb_console — no host-side capture/relay needed. */

  int pipes[MAX_PIPE_STAGES - 1][2];
  for(int i = 0; i < N - 1; i++) {
    if(pipe(pipes[i]) < 0) {
      for(int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      (void)write(
          STDOUT_FILENO, ("vega: pipe failed\n"),
          strlen(("vega: pipe failed\n"))
      );
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
      (void)write(
          STDOUT_FILENO, ("vega: fork failed\n"),
          strlen(("vega: fork failed\n"))
      );
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
    int s  = vega_exec(node->u.binop.left);
    status = (s == 0) ? vega_exec(node->u.binop.right) : s;
    break;
  }
  case AST_OR: {
    int s  = vega_exec(node->u.binop.left);
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
  case AST_WHILE: {
    status = 0;
    while(vega_exec(node->u.while_.cond) == 0)
      status = vega_exec(node->u.while_.body);
    break;
  }
  case AST_FOR: {
    status = 0;
    for(int i = 0; i < node->u.for_.nwords; i++) {
      char *expanded = expand_word(node->u.for_.words[i]);
      if(!expanded) {
        status = 1;
        break;
      }
      vega_setvar(node->u.for_.name, expanded);
      free(expanded);
      if(node->u.for_.body)
        status = vega_exec(node->u.for_.body);
    }
    break;
  }
  case AST_LET: {
    char *v = expand_word(node->u.let_.value);
    if(!v) {
      status = 1;
    } else {
      status = (vega_setvar(node->u.let_.name, v) < 0) ? 1 : 0;
      free(v);
    }
    break;
  }
  case AST_FN: {
    /* Register, transferring ownership to the table. After the steal,
     * ast_free finds NULL pointers and does nothing. If body is already
     * NULL (e.g. an AST_FN nested inside another fn body that has been
     * called once already), this is a no-op — the function stays
     * registered from the first call. */
    if(node->u.fn.body) {
      if(fntab_set(
             node->u.fn.name, node->u.fn.arg_names, node->u.fn.n_args,
             node->u.fn.body
         ) == 0) {
        node->u.fn.name      = NULL;
        node->u.fn.arg_names = NULL;
        node->u.fn.n_args    = 0;
        node->u.fn.body      = NULL;
        status               = 0;
      } else {
        (void)write(
            STDOUT_FILENO, ("vega: function table full\n"),
            strlen(("vega: function table full\n"))
        );
        status = 1;
      }
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
