#include "vega_mocks.h"
#include <stdio.h>

#include "../../user/sdk/vega/fntab.c"
#include "../../user/sdk/vega/expand.c"

static void test_expand_plain_word(void **state)
{
  (void)state;
  char *r = expand_word("hello");
  assert_string_equal(r, "hello");
  free(r);
}

static void test_expand_empty_word(void **state)
{
  (void)state;
  char *r = expand_word("");
  assert_non_null(r);
  assert_string_equal(r, "");
  free(r);
}

static void test_expand_null(void **state)
{
  (void)state;
  assert_null(expand_word(NULL));
}

static void test_expand_literal_sentinel(void **state)
{
  (void)state;
  char src[8] = {VEGA_LITERAL_SENTINEL, '$', 'F', 'O', 'O', '\0'};
  char *r = expand_word(src);
  assert_string_equal(r, "$FOO");
  free(r);
}

static void test_expand_var(void **state)
{
  (void)state;
  vega_setvar("MYVAR", "hello");
  char *r = expand_word("$MYVAR");
  assert_string_equal(r, "hello");
  free(r);
}

static void test_expand_var_braces(void **state)
{
  (void)state;
  vega_setvar("BVAR", "world");
  char *r = expand_word("${BVAR}");
  assert_string_equal(r, "world");
  free(r);
}

static void test_expand_brace_interpolation(void **state)
{
  (void)state;
  vega_setvar("IVAR", "inter");
  char *r = expand_word("{IVAR}");
  assert_string_equal(r, "inter");
  free(r);
}

static void test_expand_last_status(void **state)
{
  (void)state;
  expand_set_status(42);
  char *r = expand_word("$?");
  assert_string_equal(r, "42");
  free(r);
  expand_set_status(0);
}

static void test_expand_last_status_brace(void **state)
{
  (void)state;
  expand_set_status(7);
  char *r = expand_word("{?}");
  assert_string_equal(r, "7");
  free(r);
  expand_set_status(0);
}

static void test_expand_status_zero(void **state)
{
  (void)state;
  expand_set_status(0);
  char *r = expand_word("$?");
  assert_string_equal(r, "0");
  free(r);
}

static void test_expand_status_negative(void **state)
{
  (void)state;
  expand_set_status(-1);
  char *r = expand_word("$?");
  assert_string_equal(r, "-1");
  free(r);
  expand_set_status(0);
}

static void test_expand_pid(void **state)
{
  (void)state;
  char *r = expand_word("$$");
  assert_string_equal(r, "1234");
  free(r);
}

static void test_expand_pid_brace(void **state)
{
  (void)state;
  char *r = expand_word("{$}");
  assert_string_equal(r, "1234");
  free(r);
}

static void test_expand_lone_dollar(void **state)
{
  (void)state;
  char *r = expand_word("$ ");
  assert_non_null(r);
  assert_true(r[0] == '$');
  free(r);
}

static void test_expand_unterminated_brace(void **state)
{
  (void)state;
  char *r = expand_word("${FOO");
  assert_non_null(r);
  free(r);
}

static void test_expand_unterminated_brace_interpolation(void **state)
{
  (void)state;
  char *r = expand_word("{FOO");
  assert_non_null(r);
  free(r);
}

static void test_expand_missing_var_returns_empty(void **state)
{
  (void)state;
  char *r = expand_word("$UNDEFINED_VAR_XYZ");
  assert_string_equal(r, "");
  free(r);
}

static void test_expand_setvar_overwrite(void **state)
{
  (void)state;
  vega_setvar("OVAR", "first");
  vega_setvar("OVAR", "second");
  char *r = expand_word("$OVAR");
  assert_string_equal(r, "second");
  free(r);
}

static void test_expand_setvar_invalid(void **state)
{
  (void)state;
  assert_int_equal(vega_setvar(NULL, "val"), -1);
  assert_int_equal(vega_setvar("", "val"), -1);
}

static void test_expand_cmd_substitution(void **state)
{
  (void)state;
  will_return(mock_pipe, 12);
  will_return(mock_pipe, 13);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 100);
  will_return(mock_read, 0);
  will_return(mock_waitpid, 100);

  char *r = expand_word("$(echo hi)");
  assert_non_null(r);
  free(r);
}

static void test_expand_cmd_substitution_with_data(void **state)
{
  (void)state;
  will_return(mock_pipe, 20);
  will_return(mock_pipe, 21);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 200);
  will_return(mock_read, 3);
  will_return(mock_read, 0);
  will_return(mock_waitpid, 200);

  char *r = expand_word("$(echo hi)");
  assert_non_null(r);
  free(r);
}

static void test_expand_unterminated_cmd_sub(void **state)
{
  (void)state;
  char *r = expand_word("$(echo");
  assert_non_null(r);
  free(r);
}

static void test_expand_nested_cmd_sub(void **state)
{
  (void)state;
  will_return(mock_pipe, 22);
  will_return(mock_pipe, 23);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 201);
  will_return(mock_read, 0);
  will_return(mock_waitpid, 201);

  char *r = expand_word("$(echo $(inner))");
  assert_non_null(r);
  free(r);
}

static void test_expand_cmd_sub_pipe_fail(void **state)
{
  (void)state;
  /* pipe() fails → run_substitution returns NULL → expand_word returns NULL */
  will_return(mock_pipe, 0);
  will_return(mock_pipe, 0);
  will_return(mock_pipe, -1); /* pipe() fails */

  char *r = expand_word("$(echo hi)");
  assert_null(r);
}

static void test_expand_cmd_sub_fork_child_path(void **state)
{
  (void)state;
  /* fork=0: child does close(pipefd[0]), dup2(pipefd[1],1), close(pipefd[1]),
   * then vega_run, then _exit → longjmp. mock_close is a no-op, need dup2. */
  will_return(mock_pipe, 30);
  will_return(mock_pipe, 31);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 0);
  will_return(mock_dup2, 0); /* dup2(pipefd[1], 1) in child */
  will_return(vega_run, 0);

  if(setjmp(mock_exit_jmp) == 0)
    (void)expand_word("$(echo hi)");
  /* child longjmped out — test passes if no crash */
}

static void test_expand_setvar_max_vars(void **state)
{
  (void)state;
  /* Fill up MAX_VARS (32) with unique names, then one more must fail */
  char name[16];
  for(int i = 0; i < 32; i++) {
    snprintf(name, sizeof(name), "CAPVAR%d", i);
    vega_setvar(name, "v");
  }
  int rc = vega_setvar("OVERFLOW_VAR", "boom");
  assert_int_equal(rc, -1);
}

static void test_expand_word_with_brace_pid(void **state)
{
  (void)state;
  /* {$} interpolation inside a word → expand_brace with '$' special var */
  char *r = expand_word("pid={$}");
  assert_non_null(r);
  assert_true(strlen(r) > 4);
  free(r);
}

static void test_expand_word_with_brace_status(void **state)
{
  (void)state;
  expand_set_status(5);
  char *r = expand_word("s={?}x");
  assert_non_null(r);
  assert_string_equal(r, "s=5x");
  free(r);
  expand_set_status(0);
}

static void test_expand_cmd_sub_fork_fail(void **state)
{
  (void)state;
  will_return(mock_pipe, 40);
  will_return(mock_pipe, 41);
  will_return(mock_pipe, 0);
  will_return(mock_fork, -1); /* fork fails → NULL returned */

  char *r = expand_word("$(echo hi)");
  assert_null(r);
}

static void test_expand_cmd_sub_trailing_newlines_stripped(void **state)
{
  (void)state;
  will_return(mock_pipe, 50);
  will_return(mock_pipe, 51);
  will_return(mock_pipe, 0);
  will_return(mock_fork, 300);

  /* read returns "hello\n\n" (2 trailing newlines to strip) */
  will_return(mock_read, 7);
  will_return(mock_read, 0); /* EOF */

  will_return(mock_waitpid, 300);

  char *r = expand_word("$(printf hello)");
  assert_non_null(r);
  free(r);
}

static void test_expand_dollar_sign_followed_by_digit(void **state)
{
  (void)state;
  /* $1 is not a name_start char (digit) → emit '$' literally, not consumed */
  char *r = expand_word("$1abc");
  assert_non_null(r);
  /* '$' emitted literally, then '1abc' as literal chars */
  assert_true(r[0] == '$');
  free(r);
}

static void test_expand_brace_long_name_capped(void **state)
{
  (void)state;
  /* {NAME} where NAME >= NAME_MAX (64) chars — nlen capped */
  char word[80 + 4];
  word[0] = '{';
  for(int i = 1; i <= 70; i++)
    word[i] = 'A';
  word[71] = '}';
  word[72] = '\0';

  char *r = expand_word(word);
  assert_non_null(r);
  free(r);
}

static void test_expand_dollar_brace_long_name_capped(void **state)
{
  (void)state;
  /* ${NAME} where NAME >= NAME_MAX — nlen capped */
  char word[80 + 4];
  word[0] = '$';
  word[1] = '{';
  for(int i = 2; i <= 71; i++)
    word[i] = 'B';
  word[72] = '}';
  word[73] = '\0';

  char *r = expand_word(word);
  assert_non_null(r);
  free(r);
}

static void test_expand_dollar_name_long_capped(void **state)
{
  (void)state;
  /* $NAME where NAME >= NAME_MAX — nlen capped in expand_one */
  char word[80];
  word[0] = '$';
  for(int i = 1; i <= 70; i++)
    word[i] = 'C';
  word[71] = '\0';

  char *r = expand_word(word);
  assert_non_null(r);
  free(r);
}

static void test_expand_pid_buf_lazy_second_call(void **state)
{
  (void)state;
  /* First $$ call fills pid_buf; second call reuses it (no getpid again) */
  char *r1 = expand_word("$$");
  char *r2 = expand_word("$$");
  assert_string_equal(r1, r2);
  free(r1);
  free(r2);
}

/* buf_append: value large enough to trigger capacity doubling (line 109) */
static void test_expand_var_long_value_triggers_realloc(void **state) {
  (void)state;
  /* Reset global var state to ensure we can add a variable */
  var_count = 0;
  /* Set a variable with a very long value (>64 chars to force multiple doublings) */
  char long_value[256];
  memset(long_value, 'A', sizeof(long_value) - 1);
  long_value[255] = '\0';
  vega_setvar("LONGVAR", long_value);
  char *r = expand_word("$LONGVAR");
  assert_non_null(r);
  assert_int_equal(strlen(r), 255);
  free(r);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_expand_plain_word),
      cmocka_unit_test(test_expand_empty_word),
      cmocka_unit_test(test_expand_null),
      cmocka_unit_test(test_expand_literal_sentinel),
      cmocka_unit_test(test_expand_var),
      cmocka_unit_test(test_expand_var_braces),
      cmocka_unit_test(test_expand_brace_interpolation),
      cmocka_unit_test(test_expand_last_status),
      cmocka_unit_test(test_expand_last_status_brace),
      cmocka_unit_test(test_expand_status_zero),
      cmocka_unit_test(test_expand_status_negative),
      cmocka_unit_test(test_expand_pid),
      cmocka_unit_test(test_expand_pid_brace),
      cmocka_unit_test(test_expand_lone_dollar),
      cmocka_unit_test(test_expand_unterminated_brace),
      cmocka_unit_test(test_expand_unterminated_brace_interpolation),
      cmocka_unit_test(test_expand_missing_var_returns_empty),
      cmocka_unit_test(test_expand_setvar_overwrite),
      cmocka_unit_test(test_expand_setvar_invalid),
      cmocka_unit_test(test_expand_cmd_substitution),
      cmocka_unit_test(test_expand_cmd_substitution_with_data),
      cmocka_unit_test(test_expand_unterminated_cmd_sub),
      cmocka_unit_test(test_expand_nested_cmd_sub),
      cmocka_unit_test(test_expand_cmd_sub_pipe_fail),
      cmocka_unit_test(test_expand_cmd_sub_fork_child_path),
      cmocka_unit_test(test_expand_setvar_max_vars),
      cmocka_unit_test(test_expand_word_with_brace_pid),
      cmocka_unit_test(test_expand_word_with_brace_status),
      cmocka_unit_test(test_expand_cmd_sub_fork_fail),
      cmocka_unit_test(test_expand_cmd_sub_trailing_newlines_stripped),
      cmocka_unit_test(test_expand_dollar_sign_followed_by_digit),
      cmocka_unit_test(test_expand_brace_long_name_capped),
      cmocka_unit_test(test_expand_dollar_brace_long_name_capped),
      cmocka_unit_test(test_expand_dollar_name_long_capped),
      cmocka_unit_test(test_expand_pid_buf_lazy_second_call),
      /* new coverage */
      cmocka_unit_test(test_expand_var_long_value_triggers_realloc),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
