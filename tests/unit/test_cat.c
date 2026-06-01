#include "test_common.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int     mock_fprintf(FILE *stream, const char *format, ...);
int     mock_open(const char *pathname, int flags, ...);
ssize_t mock_read(int fd, void *buf, size_t count);
ssize_t mock_write(int fd, const void *buf, size_t count);
int     mock_close(int fd);

#define fprintf mock_fprintf
#define open    mock_open
#define read    mock_read
#define write   mock_write
#define close   mock_close

#include "../../user/lib/grendizer.c"

#define main cat_main
#include "../../user/bin/cat.c"
#undef main

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

ssize_t mock_write(int fd, const void *buf, size_t count)
{
  check_expected(fd);
  check_expected(count);
  return (ssize_t)count;
}

int mock_close(int fd)
{
  check_expected(fd);
  return mock_type(int);
}

static void test_cat_success(void **state)
{
  (void)state;
  char *argv[] = {"cat", "test.txt"};

  expect_string(mock_open, pathname, "test.txt");
  will_return(mock_open, 5);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 4);
  will_return(mock_read, "data");

  expect_value(mock_write, fd, STDOUT_FILENO);
  expect_value(mock_write, count, 4);

  expect_value(mock_read, fd, 5);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 5);
  will_return(mock_close, 0);

  assert_int_equal(cat_main(2, argv), 0);
}

static void test_cat_file_not_found(void **state)
{
  (void)state;
  char *argv[] = {"cat", "missing.txt"};

  expect_string(mock_open, pathname, "missing.txt");
  will_return(mock_open, -1);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "cat: cannot open '%s'\n");

  assert_int_equal(cat_main(2, argv), 1);
}

static void test_cat_multi_file_one_fails(void **state)
{
  (void)state;
  char *argv[] = {"cat", "good.txt", "bad.txt"};

  expect_string(mock_open, pathname, "good.txt");
  will_return(mock_open, 3);

  expect_value(mock_read, fd, 3);
  will_return(mock_read, 2);
  will_return(mock_read, "hi");

  expect_value(mock_write, fd, STDOUT_FILENO);
  expect_value(mock_write, count, 2);

  expect_value(mock_read, fd, 3);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 3);
  will_return(mock_close, 0);

  expect_string(mock_open, pathname, "bad.txt");
  will_return(mock_open, -1);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "cat: cannot open '%s'\n");

  assert_int_equal(cat_main(3, argv), 1);
}

static void test_cat_read_error(void **state)
{
  (void)state;
  char *argv[] = {"cat", "err.txt"};

  expect_string(mock_open, pathname, "err.txt");
  will_return(mock_open, 6);

  expect_value(mock_read, fd, 6);
  will_return(mock_read, -1);

  expect_value(mock_close, fd, 6);
  will_return(mock_close, 0);

  assert_int_equal(cat_main(2, argv), 1);
}

static void test_cat_multi_chunk(void **state)
{
  (void)state;
  char *argv[] = {"cat", "big.txt"};

  expect_string(mock_open, pathname, "big.txt");
  will_return(mock_open, 4);

  expect_value(mock_read, fd, 4);
  will_return(mock_read, 5);
  will_return(mock_read, "hello");

  expect_value(mock_write, fd, STDOUT_FILENO);
  expect_value(mock_write, count, 5);

  expect_value(mock_read, fd, 4);
  will_return(mock_read, 5);
  will_return(mock_read, "world");

  expect_value(mock_write, fd, STDOUT_FILENO);
  expect_value(mock_write, count, 5);

  expect_value(mock_read, fd, 4);
  will_return(mock_read, 0);

  expect_value(mock_close, fd, 4);
  will_return(mock_close, 0);

  assert_int_equal(cat_main(2, argv), 0);
}

static void test_cat_stdin(void **state)
{
  (void)state;
  char *argv[] = {"cat"};

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 5);
  will_return(mock_read, "hello");

  expect_value(mock_write, fd, STDOUT_FILENO);
  expect_value(mock_write, count, 5);

  expect_value(mock_read, fd, STDIN_FILENO);
  will_return(mock_read, 0);

  assert_int_equal(cat_main(1, argv), 0);
}

static void test_cat_help(void **state)
{
  (void)state;
  char *argv[] = {"cat", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(cat_main(2, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cat_success),
      cmocka_unit_test(test_cat_file_not_found),
      cmocka_unit_test(test_cat_multi_file_one_fails),
      cmocka_unit_test(test_cat_read_error),
      cmocka_unit_test(test_cat_multi_chunk),
      cmocka_unit_test(test_cat_stdin),
      cmocka_unit_test(test_cat_help),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
