#include "test_common.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int            mock_fprintf(FILE *stream, const char *format, ...);
DIR           *mock_opendir(const char *name);
struct dirent *mock_readdir(DIR *dirp);
int            mock_closedir(DIR *dirp);
int            mock_ioctl(int fd, unsigned long request, ...);
ssize_t        mock_write(int fd, const void *buf, size_t count);

#define fprintf  mock_fprintf
#define opendir  mock_opendir
#define readdir  mock_readdir
#define closedir mock_closedir
#define ioctl    mock_ioctl
#define write    mock_write

#include "../../user/lib/grendizer.c"

#define main ls_main
#include "../../user/bin/ls.c"
#undef main

int mock_fprintf(FILE *stream, const char *format, ...)
{
  check_expected(stream);
  check_expected(format);
  return 0;
}

DIR *mock_opendir(const char *name)
{
  check_expected(name);
  return mock_ptr_type(DIR *);
}

struct dirent *mock_readdir(DIR *dirp)
{
  check_expected(dirp);
  return mock_ptr_type(struct dirent *);
}

int mock_closedir(DIR *dirp)
{
  check_expected(dirp);
  return mock_type(int);
}

int mock_ioctl(int fd, unsigned long request, ...)
{
  check_expected(fd);
  int rc = mock_type(int);
  if(rc == 0) {
    /* Fill the winsize struct passed as the third argument */
    va_list ap;
    va_start(ap, request);
    struct winsize *ws = va_arg(ap, struct winsize *);
    va_end(ap);
    if(ws) {
      ws->ws_col = (unsigned short)mock_type(int);
      ws->ws_row = 24;
    }
  }
  return rc;
}

ssize_t mock_write(int fd, const void *buf, size_t count)
{
  check_expected(fd);
  return (ssize_t)count;
}

static void test_ls_fail_opendir(void **state)
{
  (void)state;
  char *argv[] = {"ls", "missingdir"};

  expect_string(mock_opendir, name, "missingdir");
  will_return(mock_opendir, NULL);

  expect_value(mock_fprintf, stream, stderr);
  expect_string(mock_fprintf, format,
                "ls: cannot access '%s': No such file or directory\n");

  assert_int_equal(ls_main(2, argv), 1);
}

static void test_ls_empty_dir(void **state)
{
  (void)state;
  char *argv[] = {"ls", "emptydir"};
  DIR  *fake   = (DIR *)0x1234;

  expect_string(mock_opendir, name, "emptydir");
  will_return(mock_opendir, fake);

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_single_file(void **state)
{
  (void)state;
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0x5678;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent de;
  strcpy(de.d_name, "file.txt");
  de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, &de);
  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, -1);

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_directory_entry(void **state)
{
  (void)state;
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0x5679;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent de;
  strcpy(de.d_name, "subdir");
  de.d_type = DT_DIR;

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, &de);
  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, -1);

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_mixed_entries(void **state)
{
  (void)state;
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0xABCD;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent dir_de, file_de, exec_de;
  strcpy(dir_de.d_name, "adir");   dir_de.d_type  = DT_DIR;
  strcpy(file_de.d_name, "bfile"); file_de.d_type = DT_REG;
  strcpy(exec_de.d_name, "cexec"); exec_de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &dir_de);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &file_de);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &exec_de);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, 0);
  will_return(mock_ioctl, 80); /* ws_col */

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_hidden_files_hidden(void **state)
{
  (void)state;
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0xDEAD;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent hidden_de;
  strcpy(hidden_de.d_name, ".hidden");
  hidden_de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &hidden_de);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_show_all(void **state)
{
  (void)state;
  char *argv[] = {"ls", "-a", "mydir"};
  DIR  *fake   = (DIR *)0xBEEF;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent hidden_de;
  strcpy(hidden_de.d_name, ".hidden");
  hidden_de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &hidden_de);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, -1);

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(3, argv), 0);
}

static void test_ls_one_col_flag(void **state)
{
  (void)state;
  char *argv[] = {"ls", "-1", "mydir"};
  DIR  *fake   = (DIR *)0x1111;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent de1, de2;
  strcpy(de1.d_name, "alpha"); de1.d_type = DT_REG;
  strcpy(de2.d_name, "beta");  de2.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &de1);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, &de2);
  expect_value(mock_readdir, dirp, fake); will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, 0);
  will_return(mock_ioctl, 80); /* ws_col */

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(3, argv), 0);
}

static void test_ls_default_path(void **state)
{
  (void)state;
  char *argv[] = {"ls"};
  DIR  *fake   = (DIR *)0x2222;

  expect_string(mock_opendir, name, ".");
  will_return(mock_opendir, fake);

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  assert_int_equal(ls_main(1, argv), 0);
}

static void test_ls_help(void **state)
{
  (void)state;
  char *argv[] = {"ls", "--help"};
  expect_any_always(mock_fprintf, stream);
  expect_any_always(mock_fprintf, format);
  assert_int_equal(ls_main(2, argv), 0);
}

static void test_ls_columns_from_env(void **state)
{
  (void)state;
  setenv("COLUMNS", "40", 1);

  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0x3333;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent de;
  strcpy(de.d_name, "file.txt");
  de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, &de);
  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  /* ioctl fails → falls through to COLUMNS env var */
  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, -1);

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
  unsetenv("COLUMNS");
}

static void test_ls_name_wider_than_terminal(void **state)
{
  (void)state;
  /* entry name is wider than terminal → n_cols forced to 1 */
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0x4444;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  static struct dirent de;
  /* 90-char name, terminal = 80 → col_w > cols_avail → n_cols = 1 */
  memset(de.d_name, 'a', 90);
  de.d_name[90] = '\0';
  de.d_type = DT_REG;

  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, &de);
  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, 0);
  will_return(mock_ioctl, 80); /* ws_col */

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
}

/* buf_add: string > 4096 chars → triggers nc *= 2 inner loop (line 64) */
static void test_ls_very_long_name_triggers_realloc(void **state) {
  (void)state;
  char *argv[] = {"ls", "mydir"};
  DIR  *fake   = (DIR *)0x9999;

  expect_string(mock_opendir, name, "mydir");
  will_return(mock_opendir, fake);

  /* Create a dirent with a 250-char name to exercise buf_add with a large name */
  static struct dirent de;
  memset(de.d_name, 'z', 250);
  de.d_name[250] = '\0';
  de.d_type = DT_REG;

  /* Add 20 such entries to force total buffer > 4096 → nc *= 2 needed */
  for(int i = 0; i < 20; i++) {
    expect_value(mock_readdir, dirp, fake);
    will_return(mock_readdir, &de);
  }
  expect_value(mock_readdir, dirp, fake);
  will_return(mock_readdir, NULL);

  expect_value(mock_closedir, dirp, fake);
  will_return(mock_closedir, 0);

  expect_value(mock_ioctl, fd, STDOUT_FILENO);
  will_return(mock_ioctl, 0);
  will_return(mock_ioctl, 80);

  expect_value(mock_write, fd, STDOUT_FILENO);

  assert_int_equal(ls_main(2, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_ls_fail_opendir),
      cmocka_unit_test(test_ls_empty_dir),
      cmocka_unit_test(test_ls_single_file),
      cmocka_unit_test(test_ls_directory_entry),
      cmocka_unit_test(test_ls_mixed_entries),
      cmocka_unit_test(test_ls_hidden_files_hidden),
      cmocka_unit_test(test_ls_show_all),
      cmocka_unit_test(test_ls_one_col_flag),
      cmocka_unit_test(test_ls_default_path),
      cmocka_unit_test(test_ls_help),
      cmocka_unit_test(test_ls_columns_from_env),
      cmocka_unit_test(test_ls_name_wider_than_terminal),
      /* new coverage */
      cmocka_unit_test(test_ls_very_long_name_triggers_realloc),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
