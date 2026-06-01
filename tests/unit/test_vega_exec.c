#include "vega_mocks.h"
#include <stdio.h>

#include "../../user/sdk/vega/fntab.c"
#include "../../user/sdk/vega/expand.c"
#include "../../user/sdk/vega/exec.c"

static ast_t make_cmd(char **argv, int argc)
{
  ast_t n        = {0};
  n.kind         = AST_CMD;
  n.u.cmd.argv   = argv;
  n.u.cmd.argc   = argc;
  n.u.cmd.redirs = NULL;
  return n;
}

static ast_t *make_let_node(const char *name, const char *val)
{
  ast_t *n        = calloc(1, sizeof(ast_t));
  n->kind         = AST_LET;
  n->u.let_.name  = strdup(name);
  n->u.let_.value = strdup(val);
  return n;
}

static ast_t *make_builtin_cmd(const char *name)
{
  ast_t *n         = calloc(1, sizeof(ast_t));
  n->kind          = AST_CMD;
  n->u.cmd.argv    = malloc(2 * sizeof(char *));
  n->u.cmd.argv[0] = strdup(name);
  n->u.cmd.argv[1] = NULL;
  n->u.cmd.argc    = 1;
  return n;
}

static void setup_builtin_call(int retcode)
{
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, retcode);
}

static void test_exec_null_node(void **state)
{
  (void)state;
  assert_int_equal(vega_exec(NULL), 0);
}

static void test_exec_let(void **state)
{
  (void)state;
  ast_t n        = {0};
  n.kind         = AST_LET;
  n.u.let_.name  = strdup("LETVAR");
  n.u.let_.value = strdup("letval");

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("LETVAR"), "letval");

  free(n.u.let_.name);
  free(n.u.let_.value);
}

static void test_exec_cmd_zero_argc(void **state)
{
  (void)state;
  ast_t n        = {0};
  n.kind         = AST_CMD;
  n.u.cmd.argc   = 0;
  n.u.cmd.argv   = NULL;
  n.u.cmd.redirs = NULL;
  assert_int_equal(vega_exec(&n), 0);
}

static void test_exec_cmd_empty_name(void **state)
{
  (void)state;
  char src[2]   = {VEGA_LITERAL_SENTINEL, '\0'};
  char *argv[]  = {strdup(src), NULL};
  ast_t n       = make_cmd(argv, 1);
  assert_int_equal(vega_exec(&n), 0);
  free(argv[0]);
}

static void test_exec_cmd_builtin(void **state)
{
  (void)state;
  char *argv[] = {strdup("cd"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  assert_int_equal(vega_exec(&n), 0);
  free(argv[0]);
}

static void test_exec_cmd_builtin_nonzero(void **state)
{
  (void)state;
  char *argv[] = {strdup("cd"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 3);
  assert_int_equal(vega_exec(&n), 3);
  free(argv[0]);
}

static void test_exec_cmd_external_parent_wait(void **state)
{
  (void)state;
  char *argv[] = {strdup("/bin/echo"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, false);
  will_return(mock_fork, 42);
  will_return(mock_waitpid, 0);
  will_return(mock_waitpid, 42);
  assert_int_equal(vega_exec(&n), 0);
  free(argv[0]);
}

static void test_exec_cmd_external_success(void **state)
{
  (void)state;
  char *argv[] = {strdup("/bin/true"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, false);
  will_return(mock_fork, 0);
  will_return(mock_execve, -1);
  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);
  free(argv[0]);
}

static void test_exec_cmd_not_found(void **state)
{
  (void)state;
  char *argv[] = {strdup("no_such_command_xyz"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, false);
  will_return(mock_stat, -1);
  will_return(mock_stat, -1);
  will_return(mock_stat, -1);
  assert_int_equal(vega_exec(&n), 127);
  free(argv[0]);
}

static void test_exec_cmd_relative_path(void **state)
{
  (void)state;
  char *argv[] = {strdup("./myscript"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, false);
  will_return(mock_stat, 0);
  will_return(mock_fork, 55);
  will_return(mock_waitpid, 0);
  will_return(mock_waitpid, 55);
  assert_int_equal(vega_exec(&n), 0);
  free(argv[0]);
}

static void test_exec_cmd_relative_path_not_found(void **state)
{
  (void)state;
  char *argv[] = {strdup("./missing"), NULL};
  ast_t n      = make_cmd(argv, 1);
  will_return(mock_is_builtin, false);
  will_return(mock_stat, -1);
  assert_int_equal(vega_exec(&n), 127);
  free(argv[0]);
}

static void test_exec_and_left_succeeds(void **state)
{
  (void)state;
  ast_t *left  = make_let_node("AND_L", "ok");
  ast_t *right = make_let_node("AND_R", "ok");
  ast_t  n     = {0};
  n.kind           = AST_AND;
  n.u.binop.left   = left;
  n.u.binop.right  = right;

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("AND_R"), "ok");
  free(left->u.let_.name); free(left->u.let_.value); free(left);
  free(right->u.let_.name); free(right->u.let_.value); free(right);
}

static void test_exec_and_left_fails(void **state)
{
  (void)state;
  ast_t *left  = make_builtin_cmd("false");
  ast_t *right = make_let_node("AND_SKIP", "should-not-be-set");
  ast_t  n     = {0};
  n.kind          = AST_AND;
  n.u.binop.left  = left;
  n.u.binop.right = right;

  setup_builtin_call(1);
  assert_int_equal(vega_exec(&n), 1);
  assert_string_equal(expand_getvar("AND_SKIP"), "");
  free(left->u.cmd.argv[0]); free(left->u.cmd.argv); free(left);
  free(right->u.let_.name); free(right->u.let_.value); free(right);
}

static void test_exec_or_left_fails(void **state)
{
  (void)state;
  ast_t *left  = make_builtin_cmd("fail");
  ast_t *right = make_let_node("OR_R", "ran");
  ast_t  n     = {0};
  n.kind          = AST_OR;
  n.u.binop.left  = left;
  n.u.binop.right = right;

  setup_builtin_call(1);
  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("OR_R"), "ran");
  free(left->u.cmd.argv[0]); free(left->u.cmd.argv); free(left);
  free(right->u.let_.name); free(right->u.let_.value); free(right);
}

static void test_exec_or_left_succeeds(void **state)
{
  (void)state;
  ast_t *left  = make_builtin_cmd("true");
  ast_t *right = make_let_node("OR_SKIP", "should-not-be-set");
  ast_t  n     = {0};
  n.kind          = AST_OR;
  n.u.binop.left  = left;
  n.u.binop.right = right;

  setup_builtin_call(0);
  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("OR_SKIP"), "");
  free(left->u.cmd.argv[0]); free(left->u.cmd.argv); free(left);
  free(right->u.let_.name); free(right->u.let_.value); free(right);
}

static void test_exec_seq(void **state)
{
  (void)state;
  ast_t *left  = make_let_node("SEQ_L", "first");
  ast_t *right = make_let_node("SEQ_R", "second");
  ast_t  n     = {0};
  n.kind          = AST_SEQ;
  n.u.binop.left  = left;
  n.u.binop.right = right;

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("SEQ_R"), "second");
  free(left->u.let_.name); free(left->u.let_.value); free(left);
  free(right->u.let_.name); free(right->u.let_.value); free(right);
}

static void test_exec_if_cond_true(void **state)
{
  (void)state;
  ast_t *cond        = make_let_node("IF_COND_T", "c");
  ast_t *then_branch = make_let_node("IF_THEN_T", "yes");
  ast_t  n           = {0};
  n.kind              = AST_IF;
  n.u.if_.cond        = cond;
  n.u.if_.then_branch = then_branch;
  n.u.if_.else_branch = NULL;

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("IF_THEN_T"), "yes");
  free(cond->u.let_.name); free(cond->u.let_.value); free(cond);
  free(then_branch->u.let_.name); free(then_branch->u.let_.value); free(then_branch);
}

static void test_exec_if_cond_false_no_else(void **state)
{
  (void)state;
  ast_t *cond        = make_builtin_cmd("false");
  ast_t *then_branch = make_let_node("IF_THEN_SKIP", "no");
  ast_t  n           = {0};
  n.kind              = AST_IF;
  n.u.if_.cond        = cond;
  n.u.if_.then_branch = then_branch;
  n.u.if_.else_branch = NULL;

  setup_builtin_call(1);
  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("IF_THEN_SKIP"), "");
  free(cond->u.cmd.argv[0]); free(cond->u.cmd.argv); free(cond);
  free(then_branch->u.let_.name); free(then_branch->u.let_.value); free(then_branch);
}

static void test_exec_if_else(void **state)
{
  (void)state;
  ast_t *cond        = make_builtin_cmd("false2");
  ast_t *then_branch = make_let_node("IF_ELSE_THEN", "no");
  ast_t *else_branch = make_let_node("IF_ELSE_ELSE", "yes");
  ast_t  n           = {0};
  n.kind              = AST_IF;
  n.u.if_.cond        = cond;
  n.u.if_.then_branch = then_branch;
  n.u.if_.else_branch = else_branch;

  setup_builtin_call(1);
  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("IF_ELSE_ELSE"), "yes");
  free(cond->u.cmd.argv[0]); free(cond->u.cmd.argv); free(cond);
  free(then_branch->u.let_.name); free(then_branch->u.let_.value); free(then_branch);
  free(else_branch->u.let_.name); free(else_branch->u.let_.value); free(else_branch);
}

static void test_exec_while_no_iterations(void **state)
{
  (void)state;
  ast_t *cond = make_builtin_cmd("cond_fail");
  ast_t *body = make_let_node("WHILE_BODY", "ran");
  ast_t  n    = {0};
  n.kind          = AST_WHILE;
  n.u.while_.cond = cond;
  n.u.while_.body = body;

  setup_builtin_call(1);
  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("WHILE_BODY"), "");
  free(cond->u.cmd.argv[0]); free(cond->u.cmd.argv); free(cond);
  free(body->u.let_.name); free(body->u.let_.value); free(body);
}

static void test_exec_for_two_words(void **state)
{
  (void)state;
  ast_t  *body   = make_let_node("FOR_OUT", "$X");
  char  **words  = malloc(2 * sizeof(char *));
  words[0]       = strdup("alpha");
  words[1]       = strdup("beta");

  ast_t n         = {0};
  n.kind          = AST_FOR;
  n.u.for_.name   = strdup("X");
  n.u.for_.words  = words;
  n.u.for_.nwords = 2;
  n.u.for_.body   = body;

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("X"), "beta");
  assert_string_equal(expand_getvar("FOR_OUT"), "beta");

  free(n.u.for_.name);
  free(words[0]); free(words[1]); free(words);
  free(body->u.let_.name); free(body->u.let_.value); free(body);
}

static void test_exec_for_no_words(void **state)
{
  (void)state;
  ast_t *body     = make_let_node("FOR_SKIP", "ran");
  ast_t  n        = {0};
  n.kind          = AST_FOR;
  n.u.for_.name   = strdup("Y");
  n.u.for_.words  = NULL;
  n.u.for_.nwords = 0;
  n.u.for_.body   = body;

  assert_int_equal(vega_exec(&n), 0);
  assert_string_equal(expand_getvar("FOR_SKIP"), "");
  free(n.u.for_.name);
  free(body->u.let_.name); free(body->u.let_.value); free(body);
}

static void test_exec_fn_register(void **state)
{
  (void)state;
  ast_t *body = make_let_node("FN_RAN", "yes");
  ast_t  n    = {0};
  n.kind           = AST_FN;
  n.u.fn.name      = strdup("myfn");
  n.u.fn.arg_names = NULL;
  n.u.fn.n_args    = 0;
  n.u.fn.body      = body;

  assert_int_equal(vega_exec(&n), 0);
  assert_null(n.u.fn.body);

  char *argv2[] = {strdup("myfn"), NULL};
  ast_t call    = make_cmd(argv2, 1);
  assert_int_equal(vega_exec(&call), 0);
  assert_string_equal(expand_getvar("FN_RAN"), "yes");
  free(argv2[0]);
  free(n.u.fn.name);
}

static void test_exec_fn_already_null_body(void **state)
{
  (void)state;
  ast_t n       = {0};
  n.kind        = AST_FN;
  n.u.fn.name   = strdup("ghostfn");
  n.u.fn.body   = NULL;
  assert_int_equal(vega_exec(&n), 0);
  free(n.u.fn.name);
}

static void test_exec_fn_with_args(void **state)
{
  (void)state;
  ast_t  *body     = make_let_node("GREETING", "$name");
  char  **argnames = malloc(sizeof(char *));
  argnames[0]      = strdup("name");

  ast_t fn_node            = {0};
  fn_node.kind             = AST_FN;
  fn_node.u.fn.name        = strdup("greet");
  fn_node.u.fn.arg_names   = argnames;
  fn_node.u.fn.n_args      = 1;
  fn_node.u.fn.body        = body;

  vega_exec(&fn_node);
  free(fn_node.u.fn.name);

  char *argv2[] = {strdup("greet"), strdup("Alice"), NULL};
  ast_t call    = {0};
  call.kind       = AST_CMD;
  call.u.cmd.argv = argv2;
  call.u.cmd.argc = 2;

  assert_int_equal(vega_exec(&call), 0);
  assert_string_equal(expand_getvar("GREETING"), "Alice");
  free(argv2[0]); free(argv2[1]);
}

static void test_exec_pipe_two_stages(void **state)
{
  (void)state;
  ast_t *s1 = make_builtin_cmd("echo");
  ast_t *s2 = make_builtin_cmd("cat");

  ast_t **stages = malloc(2 * sizeof(ast_t *));
  stages[0] = s1;
  stages[1] = s2;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 2;

  will_return(mock_pipe, 5);
  will_return(mock_pipe, 6);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 10);
  will_return(mock_fork, 11);
  will_return(mock_waitpid, 0);
  will_return(mock_waitpid, 10);
  will_return(mock_waitpid, 0);
  will_return(mock_waitpid, 11);

  assert_int_equal(vega_exec(&n), 0);
  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(s2->u.cmd.argv[0]); free(s2->u.cmd.argv); free(s2);
  free(stages);
}

static void test_exec_pipe_single_stage(void **state)
{
  (void)state;
  ast_t  *s1     = make_builtin_cmd("echo");
  ast_t **stages = malloc(sizeof(ast_t *));
  stages[0]      = s1;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 77);
  will_return(mock_waitpid, 0);
  will_return(mock_waitpid, 77);

  assert_int_equal(vega_exec(&n), 0);
  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(stages);
}

static void test_exec_cmd_builtin_with_out_redir(void **state)
{
  (void)state;
  char    *target = strdup("/tmp/out.txt");
  redir_t  r      = {REDIR_OUT, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_dup, 10);
  will_return(mock_dup, 11);
  will_return(mock_open, 3);
  will_return(mock_dup2, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_cmd_builtin_with_in_redir(void **state)
{
  (void)state;
  char    *target = strdup("/tmp/in.txt");
  redir_t  r      = {REDIR_IN, target, NULL};
  ast_t   *n      = make_builtin_cmd("cat");
  n->u.cmd.redirs = &r;

  will_return(mock_dup, 20);
  will_return(mock_dup, 21);
  will_return(mock_open, 4);
  will_return(mock_dup2, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_cmd_builtin_with_append_redir(void **state)
{
  (void)state;
  char    *target = strdup("/tmp/append.txt");
  redir_t  r      = {REDIR_APPEND, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_dup, 30);
  will_return(mock_dup, 31);
  will_return(mock_open, 7);
  will_return(mock_dup2, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_cmd_builtin_redir_open_fail(void **state)
{
  (void)state;
  char    *target = strdup("/no/such/path");
  redir_t  r      = {REDIR_OUT, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, 40);
  will_return(mock_dup, 41);
  will_return(mock_open, -1);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), -1);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_cmd_builtin_herestring(void **state)
{
  (void)state;
  char    *content = strdup("hello world");
  redir_t  r       = {REDIR_HERESTRING, content, NULL};
  ast_t   *n       = make_builtin_cmd("cat");
  n->u.cmd.redirs  = &r;

  will_return(mock_dup, 50);
  will_return(mock_dup, 51);
  will_return(mock_pipe, 8);
  will_return(mock_pipe, 9);
  will_return(mock_pipe, 0);
  will_return(mock_dup2, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(content);
}

static void test_exec_cmd_builtin_heredoc(void **state)
{
  (void)state;
  char    *content = strdup("line1\nline2\n");
  redir_t  r       = {REDIR_HEREDOC, content, NULL};
  ast_t   *n       = make_builtin_cmd("cat");
  n->u.cmd.redirs  = &r;

  will_return(mock_dup, 60);
  will_return(mock_dup, 61);
  will_return(mock_pipe, 14);
  will_return(mock_pipe, 15);
  will_return(mock_pipe, 0);
  will_return(mock_dup2, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  assert_int_equal(vega_exec(n), 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(content);
}

static void test_exec_while_one_iteration(void **state)
{
  (void)state;
  /* cond succeeds once then fails → body runs once */
  ast_t *cond = make_builtin_cmd("cond");
  ast_t *body = make_let_node("WHILE1", "ran");
  ast_t  n    = {0};
  n.kind          = AST_WHILE;
  n.u.while_.cond = cond;
  n.u.while_.body = body;

  /* first call to cond: success (0) → body runs */
  setup_builtin_call(0);
  /* second call to cond: fail (1) → loop exits */
  setup_builtin_call(1);

  int rc = vega_exec(&n);
  assert_int_equal(rc, 0);
  assert_string_equal(expand_getvar("WHILE1"), "ran");
  free(cond->u.cmd.argv[0]); free(cond->u.cmd.argv); free(cond);
  free(body->u.let_.name); free(body->u.let_.value); free(body);
}

static void test_exec_pipeline_too_long(void **state)
{
  (void)state;
  /* N > MAX_PIPE_STAGES (16) → returns 1 with error message */
  int     N      = 17;
  ast_t **stages = calloc(N, sizeof(ast_t *));
  for(int i = 0; i < N; i++)
    stages[i] = make_builtin_cmd("echo");

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = N;

  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);

  for(int i = 0; i < N; i++) {
    free(stages[i]->u.cmd.argv[0]);
    free(stages[i]->u.cmd.argv);
    free(stages[i]);
  }
  free(stages);
}

static void test_exec_fn_table_full(void **state)
{
  (void)state;
  /* Fill the fntab (MAX_FUNCTIONS=16) then try to register one more */
  for(int i = 0; i < 16; i++) {
    char name[16];
    snprintf(name, sizeof(name), "fn%d", i);
    ast_t n      = {0};
    n.kind        = AST_FN;
    n.u.fn.name   = strdup(name);
    n.u.fn.body   = make_let_node("X", "y");
    vega_exec(&n);
    free(n.u.fn.name);
  }

  /* 17th registration must fail */
  ast_t n      = {0};
  n.kind        = AST_FN;
  n.u.fn.name   = strdup("overflow_fn");
  n.u.fn.body   = make_let_node("OVF", "yes");
  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);
  free(n.u.fn.name);
  free(((ast_t *)n.u.fn.body)->u.let_.name);
  free(((ast_t *)n.u.fn.body)->u.let_.value);
  free(n.u.fn.body);
}

static void test_exec_builtin_with_redir_saved_in_negative(void **state)
{
  (void)state;
  /* When dup(0) returns -1 (no fd 0), restore path uses close(0) instead */
  char    *target = strdup("/tmp/out.txt");
  redir_t  r      = {REDIR_OUT, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, -1); /* saved_in = -1 */
  will_return(mock_dup, -1); /* saved_out = -1 */
  will_return(mock_open, 3);
  will_return(mock_dup2, 0);
  will_return(mock_run_builtin, 0);
  /* restore: close(0) and close(1) instead of dup2 */

  int rc = vega_exec(n);
  assert_int_equal(rc, 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_fork_fail(void **state)
{
  (void)state;
  /* fork < 0 → run_external returns -1 → exec_cmd returns -1 but writes
   * "command not found" so rc is 127 */
  char *argv[] = {strdup("/bin/echo"), NULL};
  ast_t n      = make_cmd(argv, 1);

  will_return(mock_is_builtin, false);
  will_return(mock_fork, -1);

  int rc = vega_exec(&n);
  /* run_external returns -1 → exec_cmd detects ret < 0 → prints error → 127 */
  assert_int_equal(rc, 127);
  free(argv[0]);
}

static void test_exec_waitpid_fail(void **state)
{
  (void)state;
  char *argv[] = {strdup("/bin/echo"), NULL};
  ast_t n      = make_cmd(argv, 1);

  will_return(mock_is_builtin, false);
  will_return(mock_fork, 99);
  /* waitpid(99, &status, 0): status is non-NULL so mock consumes status first */
  will_return(mock_waitpid, 0);    /* *status = 0 */
  will_return(mock_waitpid, -1);   /* return value < 0 → run_external returns -1 */

  int rc = vega_exec(&n);
  assert_int_equal(rc, 127);
  free(argv[0]);
}

static void test_exec_default_ast_kind(void **state)
{
  (void)state;
  ast_t n = {0};
  n.kind  = (ast_kind_t)999;
  assert_int_equal(vega_exec(&n), 0);
}

static void test_exec_redir_default_kind(void **state)
{
  (void)state;
  /* REDIR kind not in switch (e.g. out-of-range) → default → return -1 */
  char    *target = strdup("dummy");
  redir_t  r      = {(redir_kind_t)99, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, 10);
  will_return(mock_dup, 11);
  /* apply_one_redir hits default → frees target → returns -1 */
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  int rc = vega_exec(n);
  assert_int_equal(rc, -1);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  /* target was freed inside apply_one_redir, do not double-free */
}

static void test_exec_redir_dup2_fail(void **state)
{
  (void)state;
  /* dup2(fd, dest_fd) fails → apply_one_redir returns -1 */
  char    *target = strdup("/tmp/out.txt");
  redir_t  r      = {REDIR_OUT, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, 20);
  will_return(mock_dup, 21);
  will_return(mock_open, 5);
  will_return(mock_dup2, -1); /* dup2 fails */
  /* restore */
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  int rc = vega_exec(n);
  assert_int_equal(rc, -1);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_pipe_fail_in_pipeline(void **state)
{
  (void)state;
  ast_t *s1 = make_builtin_cmd("echo");
  ast_t *s2 = make_builtin_cmd("cat");

  ast_t **stages = malloc(2 * sizeof(ast_t *));
  stages[0] = s1;
  stages[1] = s2;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 2;

  /* pipe() fails on the first call */
  will_return(mock_pipe, 0);
  will_return(mock_pipe, 0);
  will_return(mock_pipe, -1);

  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);

  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(s2->u.cmd.argv[0]); free(s2->u.cmd.argv); free(s2);
  free(stages);
}

static void test_exec_fn_with_redir(void **state)
{
  (void)state;
  /* Overwrite an existing slot ("myfn" registered earlier) so the table
   * is not full when we get here — use a name we know exists */
  ast_t *body = make_let_node("FN_REDIR_OUT", "yes");
  char  *name = strdup("myfn"); /* registered in test_exec_fn_register */
  int    ok   = fntab_set(name, NULL, 0, body);
  if(ok != 0) {
    /* Table unexpectedly full — skip rather than mislead */
    free(name);
    free(body->u.let_.name); free(body->u.let_.value); free(body);
    skip();
    return;
  }

  char    *target = strdup("/tmp/fnout.txt");
  redir_t  r      = {REDIR_OUT, target, NULL};

  ast_t *call         = calloc(1, sizeof(ast_t));
  call->kind          = AST_CMD;
  call->u.cmd.argv    = malloc(2 * sizeof(char *));
  call->u.cmd.argv[0] = strdup("myfn");
  call->u.cmd.argv[1] = NULL;
  call->u.cmd.argc    = 1;
  call->u.cmd.redirs  = &r;

  will_return(mock_dup, 50);
  will_return(mock_dup, 51);
  will_return(mock_open, 7);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  int rc = vega_exec(call);
  assert_int_equal(rc, 0);
  assert_string_equal(expand_getvar("FN_REDIR_OUT"), "yes");

  free(call->u.cmd.argv[0]); free(call->u.cmd.argv); free(call);
  free(target);
  free(name);
}

static void test_exec_for_body_null(void **state)
{
  (void)state;
  /* for loop with NULL body — still iterates, just does nothing per word */
  char **words = malloc(2 * sizeof(char *));
  words[0]     = strdup("a");
  words[1]     = strdup("b");

  ast_t n         = {0};
  n.kind          = AST_FOR;
  n.u.for_.name   = strdup("NULLBODY");
  n.u.for_.words  = words;
  n.u.for_.nwords = 2;
  n.u.for_.body   = NULL;

  int rc = vega_exec(&n);
  assert_int_equal(rc, 0);
  assert_string_equal(expand_getvar("NULLBODY"), "b");

  free(n.u.for_.name);
  free(words[0]); free(words[1]); free(words);
}

static void test_exec_let_setvar_fail(void **state)
{
  (void)state;
  /* Fill MAX_VARS so vega_setvar fails → AST_LET returns status=1 */
  for(int i = 0; i < 32; i++) {
    char name[16];
    snprintf(name, sizeof(name), "LV%d", i);
    vega_setvar(name, "x");
  }

  ast_t n        = {0};
  n.kind         = AST_LET;
  n.u.let_.name  = strdup("OVERFLOW_LET");
  n.u.let_.value = strdup("v");

  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);

  free(n.u.let_.name);
  free(n.u.let_.value);
}

static void test_exec_fail_fast(void **state)
{
  (void)state;
  /* fail_fast=1 + ret!=0 → copies name, writes message, calls exit(ret) */
  char *argv_arr[] = {strdup("/bin/false"), NULL};
  ast_t n          = make_cmd(argv_arr, 1);
  n.u.cmd.fail_fast = 1;

  will_return(mock_is_builtin, false);
  will_return(mock_fork, 33);
  will_return(mock_waitpid, 1 << 8); /* exit code 1 */
  will_return(mock_waitpid, 33);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);
  /* longjmped out via exit(ret) — test passes if no crash */
  free(argv_arr[0]);
}

static void test_exec_pipeline_fork_fail(void **state)
{
  (void)state;
  /* fork fails on second stage — pipeline cleans up pipes and returns 1 */
  ast_t *s1 = make_builtin_cmd("echo");
  ast_t *s2 = make_builtin_cmd("cat");

  ast_t **stages = malloc(2 * sizeof(ast_t *));
  stages[0] = s1;
  stages[1] = s2;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 2;

  will_return(mock_pipe, 5);
  will_return(mock_pipe, 6);
  will_return(mock_pipe, 0);

  /* stage 0 fork succeeds → parent */
  will_return(mock_fork, 10);
  /* stage 1 fork fails → pipeline returns 1 immediately, no waitpid */
  will_return(mock_fork, -1);

  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);

  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(s2->u.cmd.argv[0]); free(s2->u.cmd.argv); free(s2);
  free(stages);
}

static void test_exec_stage_in_child_non_cmd(void **state)
{
  (void)state;
  /* Pipeline stage that is a non-CMD node (e.g. AST_LET) runs via vega_exec
   * inside the child process, then _exit */
  ast_t *let_stage = make_let_node("STAGE_VAR", "set");

  ast_t **stages = malloc(sizeof(ast_t *));
  stages[0]      = let_stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  /* fork → child (pid=0) → exec_stage_in_child for non-CMD → vega_exec →
   * AST_LET sets var → _exit(0) → longjmp */
  will_return(mock_fork, 0);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(let_stage->u.let_.name);
  free(let_stage->u.let_.value);
  free(let_stage);
  free(stages);
}

static void test_exec_stage_cmd_zero_argc_in_child(void **state)
{
  (void)state;
  /* exec_stage_in_child with AST_CMD argc==0 → _exit(0) */
  ast_t *stage    = calloc(1, sizeof(ast_t));
  stage->kind     = AST_CMD;
  stage->u.cmd.argv  = NULL;
  stage->u.cmd.argc  = 0;

  ast_t **stages = malloc(sizeof(ast_t *));
  stages[0]      = stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 0);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(stage); free(stages);
}

static void test_exec_stage_cmd_builtin_in_child(void **state)
{
  (void)state;
  /* exec_stage_in_child: builtin command → run_builtin → _exit */
  ast_t *stage      = make_builtin_cmd("cd");
  ast_t **stages    = malloc(sizeof(ast_t *));
  stages[0]         = stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 0);
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(stage->u.cmd.argv[0]); free(stage->u.cmd.argv); free(stage);
  free(stages);
}

static void test_exec_stage_cmd_fn_in_child(void **state)
{
  (void)state;
  /* exec_stage_in_child: fn lookup succeeds → call_function → _exit(rc)
   * "fn0" is always in the table after test_exec_fn_table_full fills it */
  ast_t *stage      = make_builtin_cmd("fn0");
  ast_t **stages    = malloc(sizeof(ast_t *));
  stages[0]         = stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 0);
  /* fn0 is found in fntab → call_function → _exit(0) — no is_builtin call */

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(stage->u.cmd.argv[0]); free(stage->u.cmd.argv); free(stage);
  free(stages);
}

static void test_exec_stage_cmd_not_found_in_child(void **state)
{
  (void)state;
  /* exec_stage_in_child: no fn, not builtin, resolve_path fails → _exit(127) */
  ast_t *stage      = make_builtin_cmd("no_such_cmd_xyz");
  ast_t **stages    = malloc(sizeof(ast_t *));
  stages[0]         = stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 0);
  will_return(mock_is_builtin, false);
  will_return(mock_stat, -1);
  will_return(mock_stat, -1);
  will_return(mock_stat, -1);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(stage->u.cmd.argv[0]); free(stage->u.cmd.argv); free(stage);
  free(stages);
}

static void test_exec_stage_cmd_external_in_child(void **state)
{
  (void)state;
  /* exec_stage_in_child: resolve_path succeeds → child_argv0 → execve → _exit */
  ast_t *stage      = make_builtin_cmd("/bin/echo");
  ast_t **stages    = malloc(sizeof(ast_t *));
  stages[0]         = stage;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 1;

  will_return(mock_fork, 0);
  will_return(mock_is_builtin, false);
  will_return(mock_execve, -1);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(stage->u.cmd.argv[0]); free(stage->u.cmd.argv); free(stage);
  free(stages);
}

static void test_exec_builtin_redir_dup_returns_neg1(void **state)
{
  (void)state;
  /* run_in_process_redirected: dup(0)=-1 dup(1)=-1 → close(0) close(1) on restore */
  char    *target = strdup("/tmp/out.txt");
  redir_t  r      = {REDIR_OUT, target, NULL};
  ast_t   *n      = make_builtin_cmd("echo");
  n->u.cmd.redirs = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, -1); /* saved_in = -1 */
  will_return(mock_dup, -1); /* saved_out = -1 */
  will_return(mock_open, 3);
  will_return(mock_dup2, 0);
  will_return(mock_run_builtin, 0);
  /* restore: close(0), close(1) instead of dup2 */

  int rc = vega_exec(n);
  assert_int_equal(rc, 0);
  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(target);
}

static void test_exec_pipeline_second_pipe_fail(void **state)
{
  (void)state;
  /* 3-stage pipeline: first pipe succeeds, second fails → cleans up first */
  ast_t *s1 = make_builtin_cmd("echo");
  ast_t *s2 = make_builtin_cmd("cat");
  ast_t *s3 = make_builtin_cmd("wc");

  ast_t **stages = malloc(3 * sizeof(ast_t *));
  stages[0] = s1; stages[1] = s2; stages[2] = s3;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 3;

  /* first pipe succeeds */
  will_return(mock_pipe, 5); will_return(mock_pipe, 6);
  will_return(mock_pipe, 0);
  /* second pipe fails */
  will_return(mock_pipe, 0); will_return(mock_pipe, 0);
  will_return(mock_pipe, -1);

  int rc = vega_exec(&n);
  assert_int_equal(rc, 1);

  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(s2->u.cmd.argv[0]); free(s2->u.cmd.argv); free(s2);
  free(s3->u.cmd.argv[0]); free(s3->u.cmd.argv); free(s3);
  free(stages);
}

static void test_exec_pipeline_child_fork_plumbing(void **state)
{
  (void)state;
  /* 2-stage pipeline, second fork=0: child does dup2(pipe[0],0), then
   * exec_stage_in_child → longjmp */
  ast_t *s1 = make_builtin_cmd("echo");
  ast_t *s2 = make_builtin_cmd("cat");

  ast_t **stages = malloc(2 * sizeof(ast_t *));
  stages[0] = s1; stages[1] = s2;

  ast_t n             = {0};
  n.kind              = AST_PIPE;
  n.u.pipeline.stages = stages;
  n.u.pipeline.n      = 2;

  will_return(mock_pipe, 5); will_return(mock_pipe, 6);
  will_return(mock_pipe, 0);

  /* stage 0: parent */
  will_return(mock_fork, 10);
  /* stage 1: child → plumbing dup2s then exec_stage_in_child → _exit */
  will_return(mock_fork, 0);
  will_return(mock_dup2, 0); /* dup2(pipes[0][0], 0) */
  will_return(mock_is_builtin, true);
  will_return(mock_run_builtin, 0);

  if(setjmp(mock_exit_jmp) == 0)
    vega_exec(&n);

  free(s1->u.cmd.argv[0]); free(s1->u.cmd.argv); free(s1);
  free(s2->u.cmd.argv[0]); free(s2->u.cmd.argv); free(s2);
  free(stages);
}

static void test_exec_apply_pipe_input_dup2_fail(void **state)
{
  (void)state;
  /* apply_pipe_input: dup2(pipefd[0], 0) fails → returns -1 */
  char    *content = strdup("text");
  redir_t  r       = {REDIR_HERESTRING, content, NULL};
  ast_t   *n       = make_builtin_cmd("cat");
  n->u.cmd.redirs  = &r;

  will_return(mock_is_builtin, true);
  will_return(mock_dup, 60);
  will_return(mock_dup, 61);
  will_return(mock_pipe, 8);
  will_return(mock_pipe, 9);
  will_return(mock_pipe, 0);
  will_return(mock_dup2, -1); /* dup2 fails inside apply_pipe_input */
  /* restore */
  will_return(mock_dup2, 0);
  will_return(mock_dup2, 0);

  int rc = vega_exec(n);
  assert_int_equal(rc, -1);

  free(n->u.cmd.argv[0]); free(n->u.cmd.argv); free(n);
  free(content);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_exec_null_node),
      cmocka_unit_test(test_exec_let),
      cmocka_unit_test(test_exec_cmd_zero_argc),
      cmocka_unit_test(test_exec_cmd_empty_name),
      cmocka_unit_test(test_exec_cmd_builtin),
      cmocka_unit_test(test_exec_cmd_builtin_nonzero),
      cmocka_unit_test(test_exec_cmd_external_parent_wait),
      cmocka_unit_test(test_exec_cmd_external_success),
      cmocka_unit_test(test_exec_cmd_not_found),
      cmocka_unit_test(test_exec_cmd_relative_path),
      cmocka_unit_test(test_exec_cmd_relative_path_not_found),
      cmocka_unit_test(test_exec_and_left_succeeds),
      cmocka_unit_test(test_exec_and_left_fails),
      cmocka_unit_test(test_exec_or_left_fails),
      cmocka_unit_test(test_exec_or_left_succeeds),
      cmocka_unit_test(test_exec_seq),
      cmocka_unit_test(test_exec_if_cond_true),
      cmocka_unit_test(test_exec_if_cond_false_no_else),
      cmocka_unit_test(test_exec_if_else),
      cmocka_unit_test(test_exec_while_no_iterations),
      cmocka_unit_test(test_exec_for_two_words),
      cmocka_unit_test(test_exec_for_no_words),
      cmocka_unit_test(test_exec_fn_register),
      cmocka_unit_test(test_exec_fn_already_null_body),
      cmocka_unit_test(test_exec_fn_with_args),
      cmocka_unit_test(test_exec_pipe_two_stages),
      cmocka_unit_test(test_exec_pipe_single_stage),
      cmocka_unit_test(test_exec_cmd_builtin_with_out_redir),
      cmocka_unit_test(test_exec_cmd_builtin_with_in_redir),
      cmocka_unit_test(test_exec_cmd_builtin_with_append_redir),
      cmocka_unit_test(test_exec_cmd_builtin_redir_open_fail),
      cmocka_unit_test(test_exec_cmd_builtin_herestring),
      cmocka_unit_test(test_exec_cmd_builtin_heredoc),
      cmocka_unit_test(test_exec_while_one_iteration),
      cmocka_unit_test(test_exec_pipeline_too_long),
      cmocka_unit_test(test_exec_fn_table_full),
      cmocka_unit_test(test_exec_builtin_with_redir_saved_in_negative),
      cmocka_unit_test(test_exec_fork_fail),
      cmocka_unit_test(test_exec_waitpid_fail),
      cmocka_unit_test(test_exec_default_ast_kind),
      cmocka_unit_test(test_exec_redir_default_kind),
      cmocka_unit_test(test_exec_redir_dup2_fail),
      cmocka_unit_test(test_exec_pipe_fail_in_pipeline),
      cmocka_unit_test(test_exec_fn_with_redir),
      cmocka_unit_test(test_exec_for_body_null),
      cmocka_unit_test(test_exec_let_setvar_fail),
      cmocka_unit_test(test_exec_apply_pipe_input_dup2_fail),
      cmocka_unit_test(test_exec_fail_fast),
      cmocka_unit_test(test_exec_pipeline_fork_fail),
      cmocka_unit_test(test_exec_stage_in_child_non_cmd),

      /* exec_stage_in_child: CMD paths in child (fork=0) */
      cmocka_unit_test(test_exec_stage_cmd_zero_argc_in_child),
      cmocka_unit_test(test_exec_stage_cmd_builtin_in_child),
      cmocka_unit_test(test_exec_stage_cmd_fn_in_child),
      cmocka_unit_test(test_exec_stage_cmd_not_found_in_child),
      cmocka_unit_test(test_exec_stage_cmd_external_in_child),

      /* run_in_process_redirected: dup returns -1 */
      cmocka_unit_test(test_exec_builtin_redir_dup_returns_neg1),

      /* exec_pipeline: pipe fail after first pipe already open */
      cmocka_unit_test(test_exec_pipeline_second_pipe_fail),

      /* exec_pipeline: child fork=0 path with plumbing */
      cmocka_unit_test(test_exec_pipeline_child_fork_plumbing),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
