/**
 * Alcor2 Shell - System Wrappers
 *
 * Thin wrappers around standard libc functions.
 */

#include "shell.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/** Process Control */

/**
 * @brief Exit shell with status code
 * @param code Exit status
 */
void sh_exit(int code)
{
  exit(code);
}

/** I/O Functions */

/**
 * @brief Read from file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param len Maximum bytes to read
 * @return Bytes read, or negative on error
 */
long sh_read(int fd, void *buf, size_t len)
{
  return read(fd, buf, len);
}

/**
 * @brief Write to file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param len Bytes to write
 * @return Bytes written, or negative on error
 */
long sh_write(int fd, const void *buf, size_t len)
{
  return write(fd, buf, len);
}

int sh_ioctl(int fd, unsigned long request, void *arg)
{
  return ioctl(fd, request, arg);
}

/**
 * @brief Apply keyboard layout (same tables as kernel `kbd_layout.c`).
 *
 * Usage: ioctl with `ALCOR2_IOC_KBD_SET_LAYOUT` — see `<alcor2/kbd.h>`.
 */
void sh_kbd_layout(kbd_layout_t layout)
{
  uint32_t v = (uint32_t)layout;
  (void)sh_ioctl(0, (unsigned long)ALCOR2_IOC_KBD_SET_LAYOUT, &v);
}

/**
 * @brief Clear the terminal screen
 */
void sh_clear(void)
{
  /* ANSI escape sequence to clear screen */
  const char *clear = "\033[2J\033[H";
  write(STDOUT_FILENO, clear, 7);
}

/** Filesystem Functions */

/**
 * @brief Open a file
 * @param path File path
 * @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
 * @return File descriptor, or negative on error
 */
int sh_open(const char *path, int flags)
{
  return open(path, flags);
}

/**
 * @brief Close a file descriptor
 * @param fd File descriptor to close
 * @return 0 on success, negative on error
 */
int sh_close(int fd)
{
  return close(fd);
}

/**
 * @brief Open a directory for reading
 * @param path Directory path
 * @return Directory handle, or NULL on error
 */
DIR *sh_opendir(const char *path)
{
  return opendir(path);
}

/**
 * @brief Read next directory entry
 * @param dir Directory handle
 * @return Directory entry, or NULL at end/error
 */
struct dirent *sh_readdir(DIR *dir)
{
  return readdir(dir);
}

/**
 * @brief Close a directory handle
 * @param dir Directory handle
 * @return 0 on success, negative on error
 */
int sh_closedir(DIR *dir)
{
  return closedir(dir);
}

/**
 * @brief Get file status
 * @param path File path
 * @param st Buffer to fill with status
 * @return 0 on success, negative on error
 */
int sh_stat(const char *path, struct stat *st)
{
  return stat(path, st);
}

/**
 * @brief Create a directory
 * @param path Directory path
 * @return 0 on success, negative on error
 */
int sh_mkdir(const char *path)
{
  return mkdir(path, 0755);
}

/**
 * @brief Change current directory
 * @param path New directory path
 * @return 0 on success, negative on error
 */
int sh_chdir(const char *path)
{
  return chdir(path);
}

/**
 * @brief Get current working directory
 * @param buf Buffer to store path
 * @param size Size of buffer
 * @return Pointer to buf on success, NULL on error
 */
char *sh_getcwd(char *buf, size_t size)
{
  return getcwd(buf, size);
}

/**
 * @brief Remove a file
 * @param path File path
 * @return 0 on success, negative on error
 */
int sh_unlink(const char *path)
{
  return unlink(path);
}

/** Process Execution */

extern int   execve(const char *path, char *const argv[], char *const envp[]);
extern int   fork(void);
extern void  _exit(int status) __attribute__((noreturn));
extern int   waitpid(int pid, int *status, int options);

/**
 * @brief Run @p path as a child process and wait for it.
 *
 * Standard POSIX fork + execve + waitpid pattern: the parent forks, the child
 * exec's the binary at @p path with @p argv (POSIX-style — argv[0] should be
 * the program name), the parent waits and returns the child's exit status.
 *
 * @return Child's exit status on success, or -1 on fork/exec failure.
 */
int sh_exec(const char *path, char *const argv[])
{
  int pid = fork();
  if(pid < 0)
    return -1;
  if(pid == 0) {
    execve(path, argv, NULL);
    /* execve only returns on failure; the parent will see exit status 127. */
    _exit(127);
  }
  int status = 0;
  if(waitpid(pid, &status, 0) < 0)
    return -1;
  return (status >> 8) & 0xff;
}
