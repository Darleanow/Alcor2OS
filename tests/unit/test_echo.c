#include "test_common.h"
#include <stdio.h>
#include <string.h>

int mock_printf(const char *format, ...);
int mock_fprintf(FILE *stream, const char *format, ...);

#define printf  mock_printf
#define fprintf mock_fprintf

#include "../../user/lib/grendizer.c"

#define main echo_main
#include "../../user/bin/echo.c"
#undef main

int mock_printf(const char *format, ...)
{
  check_expected(format);
  return 0;
}

int mock_fprintf(FILE *stream, const char *format, ...)
{
  check_expected(stream);
  check_expected(format);
  return 0;
}

static void test_echo_no_args(void **state)
{
  (void)state;
  char *argv[] = {"echo"};
  expect_string(mock_printf, format, "\n");
  assert_int_equal(echo_main(1, argv), 0);
}

static void test_echo_single_arg(void **state)
{
  (void)state;
  char *argv[] = {"echo", "hello"};
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, "\n");
  assert_int_equal(echo_main(2, argv), 0);
}

static void test_echo_multiple_args(void **state)
{
  (void)state;
  char *argv[] = {"echo", "hello", "world"};
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, " ");
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, "\n");
  assert_int_equal(echo_main(3, argv), 0);
}

static void test_echo_three_args(void **state)
{
  (void)state;
  char *argv[] = {"echo", "a", "b", "c"};
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, " ");
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, " ");
  expect_string(mock_printf, format, "%s");
  expect_string(mock_printf, format, "\n");
  assert_int_equal(echo_main(4, argv), 0);
}

static void test_echo_help(void **state)
{
  (void)state;
  char *argv[] = {"echo", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(echo_main(2, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_echo_no_args),
      cmocka_unit_test(test_echo_single_arg),
      cmocka_unit_test(test_echo_multiple_args),
      cmocka_unit_test(test_echo_three_args),
      cmocka_unit_test(test_echo_help),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
