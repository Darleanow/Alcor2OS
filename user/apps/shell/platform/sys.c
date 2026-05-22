/**
 * @file sys.c
 * @brief Thin syscall wrappers that back the shell platform interface.
 */

#include <dirent.h>
#include <fcntl.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Terminate the process with @p code. */
void sh_exit(int code)
{
  exit(code);
}

/** @brief Read up to @p len bytes from @p fd into @p buf. */
long sh_read(int fd, void *buf, size_t len)
{
  return read(fd, buf, len);
}

/** @brief Write @p len bytes from @p buf to @p fd. */
long sh_write(int fd, const void *buf, size_t len)
{
  return write(fd, buf, len);
}

/** @brief Issue an ioctl @p request on @p fd with @p arg. */
int sh_ioctl(int fd, unsigned long request, void *arg)
{
  return ioctl(fd, (int)request, arg); /* request fits int; cast silences narrowing */
}

/** @brief Clear the terminal screen with an ANSI erase-display sequence. */
void sh_clear(void)
{
  const char *seq = "\033[2J\033[H";
  write(STDOUT_FILENO, seq, strlen(seq));
}

/** @brief Open @p path with @p flags and return the file descriptor. */
int sh_open(const char *path, int flags)
{
  return open(path, flags);
}

/** @brief Close file descriptor @p fd. */
int sh_close(int fd)
{
  return close(fd);
}

/** @brief Open the directory at @p path and return a DIR stream. */
DIR *sh_opendir(const char *path)
{
  return opendir(path);
}

/** @brief Read the next entry from @p dir. */
struct dirent *sh_readdir(DIR *dir)
{
  return readdir(dir);
}

/** @brief Close the DIR stream @p dir. */
int sh_closedir(DIR *dir)
{
  return closedir(dir);
}

/** @brief Stat @p path and fill @p st; returns 0 on success. */
int sh_stat(const char *path, struct stat *st)
{
  return stat(path, st);
}

/** @brief Create directory @p path with mode 0755. */
int sh_mkdir(const char *path)
{
  return mkdir(path, 0755);
}

/** @brief Change the current working directory to @p path. */
int sh_chdir(const char *path)
{
  return chdir(path);
}

/** @brief Get the current working directory into @p buf of @p size bytes. */
char *sh_getcwd(char *buf, size_t size)
{
  return getcwd(buf, size);
}

/** @brief Remove the file at @p path. */
int sh_unlink(const char *path)
{
  return unlink(path);
}
