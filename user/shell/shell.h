/**
 * @file user/shell/shell.h
 * @brief Low-level helpers used across vega: string, I/O and syscall wrappers.
 */

#ifndef SHELL_H
#define SHELL_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <alcor2/kbd.h>

#define MAX_CMD_LEN 256
#define MAX_PATH    256

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
int            sh_ioctl(int fd, unsigned long request, void *arg);
void           sh_kbd_layout(kbd_layout_t layout);
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

/* Builtin commands (builtin.c). argv is POSIX-style: argv[0] is the command
 * name, argv[argc] is NULL. */
int is_builtin(const char *cmd);
int run_builtin(int argc, char *const argv[]);

#endif /* SHELL_H */
