#include "test_common.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

int mock_fprintf(FILE *stream, const char *format, ...);
int mock_printf(const char *format, ...);
int mock_ioctl(int fd, unsigned long request, ...);
void mock_perror(const char *s);

#define fprintf mock_fprintf
#define printf  mock_printf
#define ioctl   mock_ioctl
#define perror  mock_perror

#include "../../user/lib/grendizer.c"

#define main kbd_main
#include "../../user/bin/kbd.c"
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

int mock_ioctl(int fd, unsigned long request, ...)
{
  check_expected(fd);
  return mock_type(int);
}

void mock_perror(const char *s)
{
  check_expected(s);
}

static void test_kbd_us(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "us"};
  expect_value(mock_ioctl, fd, 0);
  will_return(mock_ioctl, 0);
  expect_string(mock_printf, format, "keyboard: layout %s\n");
  assert_int_equal(kbd_main(2, argv), 0);
}

static void test_kbd_fr(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "fr"};
  expect_value(mock_ioctl, fd, 0);
  will_return(mock_ioctl, 0);
  expect_string(mock_printf, format, "keyboard: layout %s\n");
  assert_int_equal(kbd_main(2, argv), 0);
}

static void test_kbd_unknown_layout(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "de"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "kbd: unknown layout '%s' (expected us|fr)\n");
  assert_int_equal(kbd_main(2, argv), 1);
}

static void test_kbd_no_args(void **state)
{
  (void)state;
  char *argv[] = {"kbd"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "usage: kbd us|fr\n");
  assert_int_equal(kbd_main(1, argv), 1);
}

static void test_kbd_too_many_args(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "us", "fr"};
  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format, "usage: kbd us|fr\n");
  assert_int_equal(kbd_main(3, argv), 1);
}

static void test_kbd_ioctl_fail(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "us"};
  expect_value(mock_ioctl, fd, 0);
  will_return(mock_ioctl, -1);
  expect_string(mock_perror, s, "kbd: ioctl");
  assert_int_equal(kbd_main(2, argv), 1);
}

static void test_kbd_help(void **state)
{
  (void)state;
  char *argv[] = {"kbd", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(kbd_main(2, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_kbd_us),
      cmocka_unit_test(test_kbd_fr),
      cmocka_unit_test(test_kbd_unknown_layout),
      cmocka_unit_test(test_kbd_no_args),
      cmocka_unit_test(test_kbd_too_many_args),
      cmocka_unit_test(test_kbd_ioctl_fail),
      cmocka_unit_test(test_kbd_help),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
