/**
 * Alcor2 Shell - Header
 *
 * Main definitions and configuration for the shell.
 */

#ifndef SHELL_H
#define SHELL_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

/** @file Configuration constants */

#define SHELL_VERSION "1.0.0"
#define MAX_CMD_LEN   256
#define MAX_ARGS      16
#define MAX_PATH      256

/** @brief Parsed command structure */

typedef struct
{
  char *cmd;            /* Command name */
  char *args[MAX_ARGS]; /* Arguments (NULL terminated) */
  int   argc;           /* Argument count */
} command_t;

/** @brief Function prototypes */

/* String utilities (str.c) */
size_t sh_strlen(const char *s);
int    sh_strcmp(const char *s1, const char *s2);
int    sh_strncmp(const char *s1, const char *s2, size_t n);
char  *sh_strcpy(char *dst, const char *src);
char  *sh_strcat(char *dst, const char *src);

/* I/O utilities (io.c) */
void sh_putchar(char c);
void sh_puts(const char *s);
void sh_putnum(long n);
int  sh_getchar(void);

/* Syscall wrappers (sys.c) */
void           sh_exit(int code);
long           sh_read(int fd, void *buf, size_t len);
long           sh_write(int fd, const void *buf, size_t len);
void           sh_clear(void);
int            sh_open(const char *path, int flags);
int            sh_close(int fd);
DIR           *sh_opendir(const char *path);
struct dirent *sh_readdir(DIR *dir);
int            sh_closedir(DIR *dir);
int            sh_stat(const char *path, struct stat *st);
int            sh_mkdir(const char *path);
int            sh_chdir(const char *path);
char          *sh_getcwd(char *buf, size_t size);
int            sh_unlink(const char *path);
int            sh_exec(const char *path, char *const argv[]);

/* Command parser (parser.c) */
int parse_command(char *line, command_t *cmd);

/* Builtin commands (builtin.c) */
int is_builtin(const char *cmd);
int run_builtin(command_t *cmd);

#endif /* SHELL_H */
