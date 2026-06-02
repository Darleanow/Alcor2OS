#include "test_common.h"
#include <stdbool.h>
#include <string.h>

/* Mock the sh_* primitives used by builtins.c */
void mock_sh_puts(const char *s);
void mock_sh_putchar(char c);
int  mock_sh_chdir(const char *path);
char *mock_sh_getcwd(char *buf, size_t size);
void mock_sh_clear(void);

#define sh_puts     mock_sh_puts
#define sh_putchar  mock_sh_putchar
#define sh_chdir    mock_sh_chdir
#define sh_getcwd   mock_sh_getcwd
#define sh_clear    mock_sh_clear

/* sh_putnum uses sh_putchar — already mocked */

#include "../../user/apps/shell/platform/builtins.c"

void mock_sh_puts(const char *s)
{
  check_expected(s);
}

void mock_sh_putchar(char c)
{
  (void)c;
}

int mock_sh_chdir(const char *path)
{
  check_expected(path);
  return mock_type(int);
}

char *mock_sh_getcwd(char *buf, size_t size)
{
  const char *val = mock_ptr_type(const char *);
  if(!val) return NULL;
  strncpy(buf, val, size - 1);
  buf[size - 1] = '\0';
  return buf;
}

void mock_sh_clear(void) {}

static void test_is_builtin_known(void **state)
{
  (void)state;
  assert_true(sh_is_builtin("exit"));
  assert_true(sh_is_builtin("cd"));
  assert_true(sh_is_builtin("pwd"));
  assert_true(sh_is_builtin("help"));
  assert_true(sh_is_builtin("version"));
  assert_true(sh_is_builtin("clear"));
}

static void test_is_builtin_unknown(void **state)
{
  (void)state;
  assert_false(sh_is_builtin("ls"));
  assert_false(sh_is_builtin("echo"));
  assert_false(sh_is_builtin(""));
  assert_false(sh_is_builtin("EXIT"));
}

static void test_builtin_list(void **state)
{
  (void)state;
  const char *const *list = sh_builtin_list();
  assert_non_null(list);
  assert_string_equal(list[0], "exit");
}

static void test_cmd_cd_success(void **state)
{
  (void)state;
  char *argv[] = {"cd", "/tmp"};
  expect_string(mock_sh_chdir, path, "/tmp");
  will_return(mock_sh_chdir, 0);
  assert_int_equal(sh_run_builtin(2, argv), 0);
}

static void test_cmd_cd_fail(void **state)
{
  (void)state;
  char *argv[] = {"cd", "/no/such/dir"};
  expect_string(mock_sh_chdir, path, "/no/such/dir");
  will_return(mock_sh_chdir, -1);
  /* sh_puts called 3 times: "cd: ", path, ": no such directory\n" */
  expect_any(mock_sh_puts, s);
  expect_any(mock_sh_puts, s);
  expect_any(mock_sh_puts, s);
  assert_int_equal(sh_run_builtin(2, argv), 1);
}

static void test_cmd_cd_no_arg_goes_to_root(void **state)
{
  (void)state;
  char *argv[] = {"cd"};
  expect_string(mock_sh_chdir, path, "/");
  will_return(mock_sh_chdir, 0);
  assert_int_equal(sh_run_builtin(1, argv), 0);
}

static void test_cmd_pwd_success(void **state)
{
  (void)state;
  char *argv[] = {"pwd"};
  will_return(mock_sh_getcwd, "/home/user");
  expect_any(mock_sh_puts, s);
  assert_int_equal(sh_run_builtin(1, argv), 0);
}

static void test_cmd_pwd_fail(void **state)
{
  (void)state;
  char *argv[] = {"pwd"};
  will_return(mock_sh_getcwd, NULL);
  expect_any(mock_sh_puts, s);
  assert_int_equal(sh_run_builtin(1, argv), 1);
}

static void test_cmd_help(void **state)
{
  (void)state;
  char *argv[] = {"help"};
  /* help prints several lines via sh_puts */
  expect_any_always(mock_sh_puts, s);
  assert_int_equal(sh_run_builtin(1, argv), 0);
}

static void test_cmd_version(void **state)
{
  (void)state;
  char *argv[] = {"version"};
  expect_any_always(mock_sh_puts, s);
  assert_int_equal(sh_run_builtin(1, argv), 0);
}

static void test_cmd_clear(void **state)
{
  (void)state;
  char *argv[] = {"clear"};
  assert_int_equal(sh_run_builtin(1, argv), 0);
}

static void test_cmd_unknown_returns_minus1(void **state)
{
  (void)state;
  char *argv[] = {"notabuiltin"};
  assert_int_equal(sh_run_builtin(1, argv), -1);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_is_builtin_known),
      cmocka_unit_test(test_is_builtin_unknown),
      cmocka_unit_test(test_builtin_list),
      cmocka_unit_test(test_cmd_cd_success),
      cmocka_unit_test(test_cmd_cd_fail),
      cmocka_unit_test(test_cmd_cd_no_arg_goes_to_root),
      cmocka_unit_test(test_cmd_pwd_success),
      cmocka_unit_test(test_cmd_pwd_fail),
      cmocka_unit_test(test_cmd_help),
      cmocka_unit_test(test_cmd_version),
      cmocka_unit_test(test_cmd_clear),
      cmocka_unit_test(test_cmd_unknown_returns_minus1),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
