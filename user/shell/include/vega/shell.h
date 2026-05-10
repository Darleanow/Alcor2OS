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
/** Raw stdout: FB TTY when active in this process, else write(1). */
void sh_stdout_bytes(const void *buf, size_t len);

/**
 * @brief Read one byte from stdin; when FB TTY is active, uses @p idle_ms
 * select timeout so @c sh_fb_tty_cursor_poll() can run while waiting.
 */
int sh_getchar_blinking(int idle_ms);
int sh_getchar(void);

/* Syscall wrappers (sys.c) */
void sh_exit(int code);
/** Switch stdin to raw mode (ICANON=0, ECHO=0, VMIN=1): shell does its own line
 * editing. */
void           sh_set_stdin_raw(void);
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

/* Builtin commands (builtin.c). argv is POSIX-style: argv[0] is the command
 * name, argv[argc] is NULL. */
int is_builtin(const char *cmd);
int run_builtin(int argc, char *const argv[]);

#endif /* SHELL_H */
