#include "test_common.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int  mock_fprintf(FILE *stream, const char *format, ...);
int  mock_printf(const char *format, ...);
int  mock_open(const char *pathname, int flags, ...);
int  mock_close(int fd);
int  mock_unlink(const char *pathname);
int  mock_mkdir(const char *pathname, mode_t mode);
char *mock_getcwd(char *buf, size_t size);

#define fprintf  mock_fprintf
#define printf   mock_printf
#define open     mock_open
#define close    mock_close
#define unlink   mock_unlink
#define mkdir    mock_mkdir
#define getcwd   mock_getcwd

#include "../../user/lib/grendizer.c"

#define main pwd_main
#include "../../user/bin/pwd.c"
#undef main

#define main rm_main
#include "../../user/bin/rm.c"
#undef main

#define main mkdir_main
#include "../../user/bin/mkdir.c"
#undef main

#define main touch_main
#include "../../user/bin/touch.c"
#undef main

int mock_fprintf(FILE *stream, const char *format, ...)
{
  check_expected(stream);
  check_expected(format);
  return 0;
}

int mock_printf(const char *format, ...)
{
  check_expected(format);
  return 0;
}

int mock_open(const char *pathname, int flags, ...)
{
  check_expected(pathname);
  return mock_type(int);
}

int mock_close(int fd)
{
  check_expected(fd);
  return 0;
}

int mock_unlink(const char *pathname)
{
  check_expected(pathname);
  return mock_type(int);
}

int mock_mkdir(const char *pathname, mode_t mode)
{
  check_expected(pathname);
  return mock_type(int);
}

char *mock_getcwd(char *buf, size_t size)
{
  const char *val = mock_ptr_type(const char *);
  if(!val) return NULL;
  strncpy(buf, val, size - 1);
  buf[size - 1] = '\0';
  return buf;
}

static void test_pwd_success(void **state)
{
  (void)state;
  char *argv[] = {"pwd"};
  will_return(mock_getcwd, "/home/user");
  expect_string(mock_printf, format, "%s\n");
  assert_int_equal(pwd_main(1, argv), 0);
}

static void test_pwd_fail(void **state)
{
  (void)state;
  char *argv[] = {"pwd"};
  will_return(mock_getcwd, NULL);
  assert_int_equal(pwd_main(1, argv), 0);
}

static void test_rm_success(void **state)
{
  (void)state;
  char *argv[] = {"rm", "file.txt"};
  expect_string(mock_unlink, pathname, "file.txt");
  will_return(mock_unlink, 0);
  assert_int_equal(rm_main(2, argv), 0);
}

static void test_rm_fail(void **state)
{
  (void)state;
  char *argv[] = {"rm", "missing.txt"};
  expect_string(mock_unlink, pathname, "missing.txt");
  will_return(mock_unlink, -1);
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "rm: cannot remove '%s'\n");
  assert_int_equal(rm_main(2, argv), 1);
}

static void test_rm_no_args(void **state)
{
  (void)state;
  char *argv[] = {"rm"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "rm: missing operand\n");
  assert_int_equal(rm_main(1, argv), 1);
}

static void test_rm_multi_partial_fail(void **state)
{
  (void)state;
  char *argv[] = {"rm", "good.txt", "bad.txt"};

  expect_string(mock_unlink, pathname, "good.txt");
  will_return(mock_unlink, 0);

  expect_string(mock_unlink, pathname, "bad.txt");
  will_return(mock_unlink, -1);
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "rm: cannot remove '%s'\n");

  assert_int_equal(rm_main(3, argv), 1);
}

static void test_mkdir_success(void **state)
{
  (void)state;
  char *argv[] = {"mkdir", "newdir"};
  expect_string(mock_mkdir, pathname, "newdir");
  will_return(mock_mkdir, 0);
  assert_int_equal(mkdir_main(2, argv), 0);
}

static void test_mkdir_fail(void **state)
{
  (void)state;
  char *argv[] = {"mkdir", "exists"};
  expect_string(mock_mkdir, pathname, "exists");
  will_return(mock_mkdir, -1);
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "mkdir: cannot create '%s'\n");
  assert_int_equal(mkdir_main(2, argv), 1);
}

static void test_mkdir_no_args(void **state)
{
  (void)state;
  char *argv[] = {"mkdir"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "mkdir: missing operand\n");
  assert_int_equal(mkdir_main(1, argv), 1);
}

static void test_mkdir_multi(void **state)
{
  (void)state;
  char *argv[] = {"mkdir", "a", "b"};
  expect_string(mock_mkdir, pathname, "a"); will_return(mock_mkdir, 0);
  expect_string(mock_mkdir, pathname, "b"); will_return(mock_mkdir, 0);
  assert_int_equal(mkdir_main(3, argv), 0);
}

static void test_touch_success(void **state)
{
  (void)state;
  char *argv[] = {"touch", "newfile"};
  expect_string(mock_open, pathname, "newfile");
  will_return(mock_open, 5);
  expect_value(mock_close, fd, 5);
  assert_int_equal(touch_main(2, argv), 0);
}

static void test_touch_fail(void **state)
{
  (void)state;
  char *argv[] = {"touch", "/ro/file"};
  expect_string(mock_open, pathname, "/ro/file");
  will_return(mock_open, -1);
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "touch: cannot create '%s'\n");
  assert_int_equal(touch_main(2, argv), 1);
}

static void test_touch_no_args(void **state)
{
  (void)state;
  char *argv[] = {"touch"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "touch: missing operand\n");
  assert_int_equal(touch_main(1, argv), 1);
}

static void test_touch_multi_partial_fail(void **state)
{
  (void)state;
  char *argv[] = {"touch", "ok.txt", "fail.txt"};

  expect_string(mock_open, pathname, "ok.txt");
  will_return(mock_open, 3);
  expect_value(mock_close, fd, 3);

  expect_string(mock_open, pathname, "fail.txt");
  will_return(mock_open, -1);
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "touch: cannot create '%s'\n");

  assert_int_equal(touch_main(3, argv), 1);
}

static void test_pwd_does_not_use_grendizer(void **state)
{
  (void)state;
  /* pwd has no grendizer, just check argc=0 path doesn't crash */
  char *argv[] = {"pwd", NULL};
  will_return(mock_getcwd, "/");
  expect_string(mock_printf, format, "%s\n");
  assert_int_equal(pwd_main(1, argv), 0);
}

static void test_rm_help(void **state)
{
  (void)state;
  char *argv[] = {"rm", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(rm_main(2, argv), 0);
}

static void test_rm_bad_option(void **state)
{
  (void)state;
  char *argv[] = {"rm", "--no-such-option"};
  assert_int_equal(rm_main(2, argv), 1);
}

static void test_mkdir_help(void **state)
{
  (void)state;
  char *argv[] = {"mkdir", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(mkdir_main(2, argv), 0);
}

static void test_mkdir_bad_option(void **state)
{
  (void)state;
  char *argv[] = {"mkdir", "--no-such-option"};
  assert_int_equal(mkdir_main(2, argv), 1);
}

static void test_touch_help(void **state)
{
  (void)state;
  char *argv[] = {"touch", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(touch_main(2, argv), 0);
}

static void test_touch_bad_option(void **state)
{
  (void)state;
  char *argv[] = {"touch", "--no-such-option"};
  assert_int_equal(touch_main(2, argv), 1);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_pwd_success),
      cmocka_unit_test(test_pwd_fail),

      cmocka_unit_test(test_rm_success),
      cmocka_unit_test(test_rm_fail),
      cmocka_unit_test(test_rm_no_args),
      cmocka_unit_test(test_rm_multi_partial_fail),

      cmocka_unit_test(test_mkdir_success),
      cmocka_unit_test(test_mkdir_fail),
      cmocka_unit_test(test_mkdir_no_args),
      cmocka_unit_test(test_mkdir_multi),

      cmocka_unit_test(test_touch_success),
      cmocka_unit_test(test_touch_fail),
      cmocka_unit_test(test_touch_no_args),
      cmocka_unit_test(test_touch_multi_partial_fail),
      cmocka_unit_test(test_pwd_does_not_use_grendizer),
      cmocka_unit_test(test_rm_help),
      cmocka_unit_test(test_rm_bad_option),
      cmocka_unit_test(test_mkdir_help),
      cmocka_unit_test(test_mkdir_bad_option),
      cmocka_unit_test(test_touch_help),
      cmocka_unit_test(test_touch_bad_option),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
