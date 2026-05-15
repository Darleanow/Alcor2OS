/**
 * @file vega/host.h
 * @brief Host interface libvega expects callers to provide.
 *
 * libvega is a freestanding language runtime; it does no syscalls of its own.
 * Each consumer (the interactive shell, the standalone vega CLI, future
 * embedders) implements this surface. The shell wires it to its framebuffer
 * tty and line editor; the CLI wires it to plain musl stdio.
 */

#ifndef VEGA_HOST_H
#define VEGA_HOST_H

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <alcor2/kbd.h>

/** Cap on path-string buffers used inside libvega (cwd, redir targets, …). */
#define MAX_PATH 256

/* String utilities */
size_t sh_strlen(const char *s);
int    sh_strcmp(const char *s1, const char *s2);
int    sh_strncmp(const char *s1, const char *s2, size_t n);
char  *sh_strcpy(char *dst, const char *src);
char  *sh_strcat(char *dst, const char *src);

/* I/O */
void sh_putchar(char c);
void sh_puts(const char *s);
void sh_putnum(long n);
/** Raw stdout bypass: FB TTY when active in this process, else write(1). */
void sh_stdout_bytes(const void *buf, size_t len);

/** Read one byte from stdin; when an FB TTY is active, uses @p idle_ms select
 * timeout so blink/cursor hooks can run while waiting. */
int sh_getchar_blinking(int idle_ms);
int sh_getchar(void);

/* Process / terminal */
void sh_exit(int code);
/** Switch stdin to raw mode (ICANON=0, ECHO=0, VMIN=1). */
void sh_set_stdin_raw(void);
long sh_read(int fd, void *buf, size_t len);
long sh_write(int fd, const void *buf, size_t len);
int  sh_ioctl(int fd, unsigned long request, void *arg);
void sh_kbd_layout(kbd_layout_t layout);
void sh_clear(void);

/* Filesystem */
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

/* FB TTY hooks libvega's exec layer needs.
 *
 * Hosts without a framebuffer terminal return false / no-op. The full FB TTY
 * surface (init/shutdown/cursor/blink) is shell-internal and lives in
 * <shell/fb_tty.h>; libvega only needs to know whether to enter pipe-relay
 * mode for child output and to tick blink while waiting. */
bool sh_fb_tty_active(void);
void sh_fb_tty_on_fork_child(void);
void sh_fb_tty_blink_tick(void);

#endif /* VEGA_HOST_H */
