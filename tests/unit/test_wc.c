#include "test_common.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int     mock_printf(const char *format, ...);
int     mock_fprintf(FILE *stream, const char *format, ...);
int     mock_open(const char *pathname, int flags, ...);
ssize_t mock_read(int fd, void *buf, size_t count);
int     mock_close(int fd);

#define printf  mock_printf
#define fprintf mock_fprintf
#define open    mock_open
#define read    mock_read
#define close   mock_close

#include "../../user/lib/grendizer.c"

#define main wc_main
#include "../../user/bin/wc.c"
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

int mock_open(const char *pathname, int flags, ...)
{
  check_expected(pathname);
  return mock_type(int);
}

ssize_t mock_read(int fd, void *buf, size_t count)
{
  check_expected(fd);
  int n = mock_type(int);
  if(n > 0) {
    const char *data = mock_ptr_type(const char *);
    memcpy(buf, data, (size_t)n);
  }
  return n;
}

int mock_close(int fd)
{
  check_expected(fd);
  return 0;
}

static void expect_printf_n(int n)
{
  for(int i = 0; i < n; i++)
    expect_any(mock_printf, format);
}

static void test_wc_default_lwb_stdin(void **state)
{
  (void)state;
  char *argv[] = {"wc"};

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 12);
  will_return(mock_read, "hello world\n");

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 0);

  expect_printf_n(4); /* lines words bytes \n */

  assert_int_equal(wc_main(1, argv), 0);
}

static void test_wc_lines_flag(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-l", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 6);
  will_return(mock_read, "a\nb\nc\n");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  expect_printf_n(3); /* lines filename \n */

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_words_flag(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-w", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 11);
  will_return(mock_read, "one two    ");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  expect_printf_n(3);

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_bytes_flag(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-c", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 4);
  will_return(mock_read, "data");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  expect_printf_n(3);

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_max_line_flag(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-L", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 10);
  will_return(mock_read, "hello\nhi\n\n");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  expect_printf_n(3);

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_chars_flag(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-m", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 3);
  will_return(mock_read, "abc");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  expect_printf_n(3);

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_open_fail(void **state)
{
  (void)state;
  char *argv[] = {"wc", "missing.txt"};

  expect_string(mock_open, pathname, "missing.txt");
  will_return(mock_open, -1);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "wc: cannot open '%s'\n");

  assert_int_equal(wc_main(2, argv), 1);
}

static void test_wc_read_error(void **state)
{
  (void)state;
  char *argv[] = {"wc", "err.txt"};

  expect_string(mock_open, pathname, "err.txt");
  will_return(mock_open, 6);

  expect_value(mock_read, fd, 6);
  will_return(mock_read, -1);

  expect_value(mock_close, fd, 6);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "wc: read error on '%s'\n");

  assert_int_equal(wc_main(2, argv), 1);
}

static void test_wc_stdin_read_error(void **state)
{
  (void)state;
  char *argv[] = {"wc"};

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, -1);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "wc: read error\n");

  assert_int_equal(wc_main(1, argv), 1);
}

static void test_wc_dash_reads_stdin(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-l", "-"};

  /* "-" as filename → STDIN_FILENO, no open/close */
  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 2);
  will_return(mock_read, "a\n");

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 0);

  expect_printf_n(3);

  assert_int_equal(wc_main(3, argv), 0);
}

static void test_wc_multi_file_total(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-l", "a.txt", "b.txt"};

  /* a.txt */
  expect_string(mock_open, pathname, "a.txt");
  will_return(mock_open, 3);
  expect_value(mock_read, fd, 3);
  will_return(mock_read, 2); will_return(mock_read, "a\n");
  expect_value(mock_read, fd, 3);
  will_return(mock_read, 0);
  expect_value(mock_close, fd, 3);
  expect_printf_n(3); /* lines a.txt \n */

  /* b.txt */
  expect_string(mock_open, pathname, "b.txt");
  will_return(mock_open, 4);
  expect_value(mock_read, fd, 4);
  will_return(mock_read, 4); will_return(mock_read, "b\nb\n");
  expect_value(mock_read, fd, 4);
  will_return(mock_read, 0);
  expect_value(mock_close, fd, 4);
  expect_printf_n(3); /* lines b.txt \n */

  /* total line */
  expect_printf_n(3);

  assert_int_equal(wc_main(4, argv), 0);
}

static void test_wc_help(void **state)
{
  (void)state;
  char *argv[] = {"wc", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(wc_main(2, argv), 0);
}

static void test_wc_all_flags(void **state)
{
  (void)state;
  char *argv[] = {"wc", "-l", "-w", "-c", "-m", "-L", "f.txt"};

  expect_string(mock_open, pathname, "f.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 12);
  will_return(mock_read, "hello world\n");

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);

  /* lines words bytes chars max_line_len filename \n */
  for(int i = 0; i < 7; i++)
    expect_any(mock_printf, format);

  assert_int_equal(wc_main(7, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_wc_default_lwb_stdin),
      cmocka_unit_test(test_wc_lines_flag),
      cmocka_unit_test(test_wc_words_flag),
      cmocka_unit_test(test_wc_bytes_flag),
      cmocka_unit_test(test_wc_max_line_flag),
      cmocka_unit_test(test_wc_chars_flag),
      cmocka_unit_test(test_wc_open_fail),
      cmocka_unit_test(test_wc_read_error),
      cmocka_unit_test(test_wc_stdin_read_error),
      cmocka_unit_test(test_wc_dash_reads_stdin),
      cmocka_unit_test(test_wc_multi_file_total),
      cmocka_unit_test(test_wc_help),
      cmocka_unit_test(test_wc_all_flags),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
