#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

/* Mock definitions */
int mock_printf(const char *format, ...);
int mock_fprintf(FILE *stream, const char *format, ...);
int mock_open(const char *pathname, int flags, ...);
ssize_t mock_read(int fd, void *buf, size_t count);
ssize_t mock_write(int fd, const void *buf, size_t count);
int mock_close(int fd);
DIR *mock_opendir(const char *name);
struct dirent *mock_readdir(DIR *dirp);
int mock_closedir(DIR *dirp);
int mock_ioctl(int fd, unsigned long request, ...);

#define printf mock_printf
#define fprintf mock_fprintf
#define open mock_open
#define read mock_read
#define write mock_write
#define close mock_close
#define opendir mock_opendir
#define readdir mock_readdir
#define closedir mock_closedir
#define ioctl mock_ioctl

#include "../../user/lib/grendizer.c"

#define main ls_main
#include "../../user/bin/ls.c"
#undef main

#define main cat_main
#include "../../user/bin/cat.c"
#undef main

#define main echo_main
#include "../../user/bin/echo.c"
#undef main

/* Implementations of mocks */
int mock_printf(const char *format, ...) {
    check_expected(format);
    return 0;
}

int mock_fprintf(FILE *stream, const char *format, ...) {
    check_expected(stream);
    check_expected(format);
    return 0;
}

int mock_open(const char *pathname, int flags, ...) {
    check_expected(pathname);
    return mock_type(int);
}

ssize_t mock_read(int fd, void *buf, size_t count) {
    check_expected(fd);
    int read_len = mock_type(int);
    if (read_len > 0) {
        const char *data = mock_ptr_type(const char *);
        memcpy(buf, data, read_len);
    }
    return read_len;
}

ssize_t mock_write(int fd, const void *buf, size_t count) {
    check_expected(fd);
    check_expected(count);
    return count;
}

int mock_close(int fd) {
    check_expected(fd);
    return mock_type(int);
}

DIR *mock_opendir(const char *name) {
    check_expected(name);
    return mock_ptr_type(DIR *);
}

struct dirent *mock_readdir(DIR *dirp) {
    check_expected(dirp);
    return mock_ptr_type(struct dirent *);
}

int mock_closedir(DIR *dirp) {
    check_expected(dirp);
    return mock_type(int);
}

int mock_ioctl(int fd, unsigned long request, ...) {
    check_expected(fd);
    return mock_type(int);
}

/* Tests */

static void test_echo_main(void **state) {
    (void)state;
    char *argv[] = {"echo", "hello", "world"};
    
    expect_string(mock_printf, format, "%s");
    expect_string(mock_printf, format, " ");
    expect_string(mock_printf, format, "%s");
    expect_string(mock_printf, format, "\n");
    
    int rc = echo_main(3, argv);
    assert_int_equal(rc, 0);
}

static void test_cat_main_success(void **state) {
    (void)state;
    char *argv[] = {"cat", "test.txt"};
    
    expect_string(mock_open, pathname, "test.txt");
    will_return(mock_open, 5); // fd = 5
    
    expect_value(mock_read, fd, 5);
    will_return(mock_read, 4);
    will_return(mock_read, "data");
    
    expect_value(mock_write, fd, STDOUT_FILENO);
    expect_value(mock_write, count, 4);
    
    expect_value(mock_read, fd, 5);
    will_return(mock_read, 0); // EOF
    
    expect_value(mock_close, fd, 5);
    will_return(mock_close, 0);
    
    int rc = cat_main(2, argv);
    assert_int_equal(rc, 0);
}

static void test_cat_main_fail(void **state) {
    (void)state;
    char *argv[] = {"cat", "missing.txt"};
    
    expect_string(mock_open, pathname, "missing.txt");
    will_return(mock_open, -1);
    
    expect_value(mock_fprintf, stream, stderr);
    expect_string(mock_fprintf, format, "cat: cannot open '%s'\n");
    
    int rc = cat_main(2, argv);
    assert_int_equal(rc, 1);
}

static void test_ls_main_success(void **state) {
    (void)state;
    char *argv[] = {"ls", "mydir"};
    
    expect_string(mock_opendir, name, "mydir");
    DIR *dummy_dir = (DIR *)0x1234;
    will_return(mock_opendir, dummy_dir);
    
    // readdir 1
    expect_value(mock_readdir, dirp, dummy_dir);
    static struct dirent de1;
    strcpy(de1.d_name, "file1");
    de1.d_type = DT_REG;
    will_return(mock_readdir, &de1);
    
    // readdir 2 (EOF)
    expect_value(mock_readdir, dirp, dummy_dir);
    will_return(mock_readdir, NULL);
    
    expect_value(mock_closedir, dirp, dummy_dir);
    will_return(mock_closedir, 0);
    
    expect_value(mock_ioctl, fd, STDOUT_FILENO);
    will_return(mock_ioctl, -1); // fallback to 80 cols
    
    expect_value(mock_write, fd, STDOUT_FILENO);
    expect_any(mock_write, count); // we write the output
    
    int rc = ls_main(2, argv);
    assert_int_equal(rc, 0);
}

static void test_ls_main_fail(void **state) {
    (void)state;
    char *argv[] = {"ls", "missingdir"};
    
    expect_string(mock_opendir, name, "missingdir");
    will_return(mock_opendir, NULL);
    
    expect_value(mock_fprintf, stream, stderr);
    expect_string(mock_fprintf, format, "ls: cannot access '%s': No such file or directory\n");
    
    int rc = ls_main(2, argv);
    assert_int_equal(rc, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_echo_main),
        cmocka_unit_test(test_cat_main_success),
        cmocka_unit_test(test_cat_main_fail),
        cmocka_unit_test(test_ls_main_success),
        cmocka_unit_test(test_ls_main_fail),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
