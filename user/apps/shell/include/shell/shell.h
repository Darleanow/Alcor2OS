/**
 * @file shell/shell.h
 * @brief Shell-internal helpers (sh_*) used by main.c, builtins.c, and the
 * shell's platform layer. main.c packages a subset of these into a
 * vega_host_ops_t to register with libvega.
 */

#ifndef SHELL_H
#define SHELL_H

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#define MAX_CMD_LEN 256
#define MAX_PATH    256

/* I/O */
void sh_putchar(char c);
void sh_puts(const char *s);
void sh_putnum(long n);
void sh_stdout_bytes(const void *buf, size_t len);

/* Process / terminal */
void sh_exit(int code);
long sh_read(int fd, void *buf, size_t len);
long sh_write(int fd, const void *buf, size_t len);
int  sh_ioctl(int fd, unsigned long request, void *arg);
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

/**
 * @brief Check whether @p name is a shell builtin command.
 *
 * @param name  Command name to look up.
 * @return true if @p name matches a registered builtin, false otherwise.
 */
bool sh_is_builtin(const char *name);

/**
 * @brief Execute a shell builtin.
 *
 * @param argc  Argument count (including the command name at @c argv[0]).
 * @param argv  NULL-terminated argument vector.
 * @return Exit status (0 = success).
 */
int sh_run_builtin(int argc, char *const argv[]);

/**
 * @brief Return the NULL-terminated array of builtin command names.
 *
 * @return Pointer to an internal static array; valid for the lifetime of the
 *         shell process.
 */
const char *const *sh_builtin_list(void);

/* Tab completion */
#define COMP_MAX      64
#define COMP_NAME_MAX 64

/**
 * @brief Accumulator for tab-completion candidates.
 *
 * Filled by ::sh_complete. @c entries holds up to ::COMP_MAX candidate
 * strings, @c count is the number actually populated, and @c common holds the
 * longest common prefix across all candidates (used for single-tab insertion).
 */
typedef struct
{
  char entries[COMP_MAX][COMP_NAME_MAX]; /**< Candidate strings. */
  int  count;                            /**< Number of valid entries. */
  char common[COMP_NAME_MAX];            /**< Longest common prefix. */
} comp_result_t;

/**
 * @brief Compute completions for @p prefix.
 *
 * @param prefix     The partial word to complete.
 * @param is_command true if completing a command name (first word), false for
 *                   path completion.
 * @param out        Filled with matching entries and the longest common prefix.
 */
void sh_complete(const char *prefix, bool is_command, comp_result_t *out);

#endif /* SHELL_H */
