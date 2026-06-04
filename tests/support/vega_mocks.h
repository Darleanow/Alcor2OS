#pragma once
#include "test_common.h"
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cmocka.h>

#include <vega/ast.h>
#include <vega/vega.h>
#include <vega/host.h>
#include <vega/internal/expand.h>
#include <vega/internal/fntab.h>
#include <vega/internal/exec.h>

int mock_fork(void) { return (int)mock(); }
#define fork mock_fork

int mock_pipe(int pipefd[2])
{
  pipefd[0] = mock_type(int);
  pipefd[1] = mock_type(int);
  return mock_type(int);
}
#define pipe mock_pipe

int mock_dup2(int oldfd, int newfd) { return mock_type(int); }
#define dup2 mock_dup2

int mock_dup(int oldfd) { return mock_type(int); }
#define dup mock_dup

ssize_t mock_read(int fd, void *buf, size_t count) { return (ssize_t)mock(); }
#define read mock_read

ssize_t mock_write(int fd, const void *buf, size_t count) { return (ssize_t)count; }
#define write mock_write

int mock_close(int fd) { return 0; }
#define close mock_close

int mock_open(const char *pathname, int flags, mode_t mode) { return mock_type(int); }
#define open mock_open

pid_t mock_waitpid(pid_t pid, int *status, int options)
{
  if(status) *status = mock_type(int);
  return mock_type(pid_t);
}
#define waitpid mock_waitpid

int mock_execve(const char *pathname, char *const argv[], char *const envp[])
{
  return mock_type(int);
}
#define execve mock_execve

jmp_buf mock_exit_jmp;
void    mock__exit(int status) { longjmp(mock_exit_jmp, status); }
#define _exit mock__exit
#define exit  mock__exit

pid_t mock_getpid(void) { return 1234; }
#define getpid mock_getpid

int mock_stat(const char *pathname, struct stat *statbuf)
{
  statbuf->st_mode = S_IFREG | 0755;
  return (int)mock();
}
#define stat(...) mock_stat(__VA_ARGS__)

void ast_free(ast_t *n) { (void)n; }

int vega_run(const char *cmd) { return mock_type(int); }

char  *envp_mock[] = {NULL};
char **environ     = envp_mock;

static bool mock_is_builtin(const char *cmd) { return (bool)mock(); }
static int  mock_run_builtin(int argc, char *const argv[]) { return mock_type(int); }
static struct vega_host_ops g_mock_host = {
    .is_builtin  = mock_is_builtin,
    .run_builtin = mock_run_builtin,
};
const struct vega_host_ops *vega_host = &g_mock_host;
