#include "test_common.h"

int open(const char *pathname, int flags) {
    check_expected(pathname);
    check_expected(flags);
    return mock_type(int);
}

int dup2(int oldfd, int newfd) {
    check_expected(oldfd);
    check_expected(newfd);
    return mock_type(int);
}

int close(int fd) {
    check_expected(fd);
    return mock_type(int);
}

int fork(void) {
    return mock_type(int);
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    check_expected(pathname);
    (void)argv;
    (void)envp;
    return mock_type(int);
}

int waitpid(int pid, int *wstatus, int options) {
    check_expected(pid);
    (void)wstatus;
    (void)options;
    return mock_type(int);
}

int printf(const char *format, ...) {
    check_expected(format);
    return mock_type(int);
}

void perror(const char *s) {
    check_expected(s);
}

#define main init_main
#include "../../user/init/main.c"
#undef main

static void test_init_main(void **state) {
    (void)state;

    expect_string(printf, format, "Hello from userspace!\n");
    will_return(printf, 22);

    expect_string(printf, format, "Alcor2 init process running in Ring 3.\n");
    will_return(printf, 39);

    int ret = init_main(0, NULL);
    assert_int_equal(ret, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_main),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
