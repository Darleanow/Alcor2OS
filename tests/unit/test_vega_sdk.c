#include "test_common.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmocka.h>

#include <vega/ast.h>
#include <vega/vega.h>
#include <vega/host.h>
#include <vega/internal/expand.h>
#include <vega/internal/fntab.h>
#include <vega/internal/exec.h>

/* mock wrappers */
int mock_fork(void) { return (int)mock(); }
#define fork mock_fork

int mock_pipe(int pipefd[2]) {
    pipefd[0] = mock_type(int);
    pipefd[1] = mock_type(int);
    return mock_type(int);
}
#define pipe mock_pipe

int mock_dup2(int oldfd, int newfd) { return mock_type(int); }
#define dup2 mock_dup2

int mock_dup(int oldfd) { return mock_type(int); }
#define dup mock_dup

ssize_t mock_read(int fd, void *buf, size_t count) { 
    return (ssize_t)mock(); 
}
#define read mock_read

ssize_t mock_write(int fd, const void *buf, size_t count) {
    return count; 
}
#define write mock_write

int mock_close(int fd) { return 0; }
#define close mock_close

int mock_open(const char *pathname, int flags, mode_t mode) { return mock_type(int); }
#define open mock_open

pid_t mock_waitpid(pid_t pid, int *status, int options) { 
    if(status) *status = mock_type(int);
    return mock_type(pid_t);
}
#define waitpid mock_waitpid

int mock_execve(const char *pathname, char *const argv[], char *const envp[]) {
    return mock_type(int);
}
#define execve mock_execve

jmp_buf mock_exit_jmp;
void mock__exit(int status) {
    longjmp(mock_exit_jmp, status);
}
#define _exit mock__exit
#define exit mock__exit

pid_t mock_getpid(void) {
    return 1234;
}
#define getpid mock_getpid

int mock_stat(const char *pathname, struct stat *statbuf) {
    return (int)mock();
}
#define stat(...) mock_stat(__VA_ARGS__)

/* Provide dummy ast_free and vega_run */
void ast_free(ast_t *n) {}
int vega_run(const char *cmd) { return mock_type(int); }

/* Provide environ */
char *envp_mock[] = {NULL};
char **environ = envp_mock;

/* vega_host mock */
static bool mock_is_builtin(const char *cmd) { return (bool)mock(); }
static int mock_run_builtin(int argc, char *const argv[]) { return mock_type(int); }
static struct vega_host_ops g_mock_host = {
    .is_builtin = mock_is_builtin,
    .run_builtin = mock_run_builtin
};
const struct vega_host_ops *vega_host = &g_mock_host;

/* Include C files directly */
#include "../../user/sdk/vega/fntab.c"
#include "../../user/sdk/vega/expand.c"
#include "../../user/sdk/vega/exec.c"

static void test_fntab(void **state) {
    (void)state;
    // Set a function and retrieve it
    char *name = strdup("testfn");
    int rc = fntab_set(name, NULL, 0, NULL);
    assert_int_equal(rc, 0);
    
    const fn_entry_t *entry = fntab_get("testfn");
    assert_non_null(entry);
    assert_string_equal(entry->name, "testfn");
}

static void test_expand_var(void **state) {
    (void)state;
    vega_setvar("FOO", "bar");
    const char *val = expand_getvar("FOO");
    assert_string_equal(val, "bar");
    
    char *exp = expand_word("$FOO");
    assert_string_equal(exp, "bar");
    free(exp);
}

static void test_vega_exec_let(void **state) {
    (void)state;
    ast_t node = {0};
    node.kind = AST_LET;
    node.u.let_.name = strdup("HELLO");
    node.u.let_.value = strdup("WORLD");
    
    int rc = vega_exec(&node);
    assert_int_equal(rc, 0);
    
    const char *val = expand_getvar("HELLO");
    assert_string_equal(val, "WORLD");
    
    free(node.u.let_.name);
    free(node.u.let_.value);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fntab),
        cmocka_unit_test(test_expand_var),
        cmocka_unit_test(test_vega_exec_let),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
