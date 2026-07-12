/**
 * @file cp.c
 * @brief Copy one or more source files to a destination file or directory.
 *
 * Usage: cp <source> [source ...] <dest>
 *
 * When the destination is an existing directory each source is copied into it
 * preserving the source basename.  When a single source is given and the
 * destination is not a directory the source is copied directly to that path.
 * Copying multiple sources to a non-directory destination is an error.
 */

#include <grendizer.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** I/O transfer buffer size used by copy_fd(). */
#define CP_BUF_SIZE 4096

/**
 * @brief Write all @p n bytes of @p buf to @p fd, retrying on short writes.
 *
 * @param fd   File descriptor open for writing.
 * @param buf  Data to write.
 * @param n    Number of bytes to write.
 * @return 0 on success, 1 on error.
 */
static int write_all(int fd, const char *buf, ssize_t n)
{
  while(n > 0) {
    ssize_t w = write(fd, buf, (size_t)n);
    if(w < 0)
      return 1;
    buf += w;
    n -= w;
  }
  return 0;
}

/**
 * @brief Copy all bytes from @p src_fd to @p dst_fd.
 *
 * @param src_fd  File descriptor open for reading.
 * @param dst_fd  File descriptor open for writing.
 * @return 0 on success, 1 on I/O error.
 */
static int copy_fd(int src_fd, int dst_fd)
{
  char    buf[CP_BUF_SIZE];
  ssize_t n;

  while((n = read(src_fd, buf, sizeof buf)) > 0) {
    if(write_all(dst_fd, buf, n))
      return 1;
  }
  return n < 0 ? 1 : 0;
}

/**
 * @brief Copy the regular file at @p src to the path @p dst.
 *
 * The destination is created or truncated.  Permissions are set to 0644.
 *
 * @param src  Source file path.
 * @param dst  Destination file path.
 * @return 0 on success, 1 on error.
 */
static int copy_file(const char *src, const char *dst)
{
  int src_fd = open(src, O_RDONLY);
  if(src_fd < 0) {
    (void)fprintf(stderr, "cp: cannot open '%s'\n", src);
    return 1;
  }

  int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(dst_fd < 0) {
    (void)fprintf(stderr, "cp: cannot create '%s'\n", dst);
    close(src_fd);
    return 1;
  }

  int rc = copy_fd(src_fd, dst_fd);
  if(rc)
    (void)fprintf(stderr, "cp: error while copying '%s' to '%s'\n", src, dst);

  close(src_fd);
  close(dst_fd);
  return rc;
}

/**
 * @brief Build "dir/basename(src)" into @p out (capacity @p cap).
 *
 * @param out  Output buffer.
 * @param cap  Capacity of @p out in bytes, including NUL.
 * @param dir  Destination directory path.
 * @param src  Source file path; its basename is appended.
 * @return 0 on success, 1 if the resulting path would exceed @p cap.
 */
static int build_dest_path(
    char *out, size_t cap, const char *dir, const char *src
)
{
  const char *base = gr_basename(src);
  size_t      dlen = strlen(dir);
  size_t      blen = strlen(base);

  if(dlen + 1 + blen + 1 > cap)
    return 1;

  memcpy(out, dir, dlen);
  out[dlen] = '/';
  memcpy(out + dlen + 1, base, blen + 1);
  return 0;
}

int main(int argc, char *argv[])
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {
        .program = "cp",
        .usage   = "<source> [source ...] <dest>",
        .options = opts,
        .epilog  = "When <dest> is a directory, each source is copied into it.\n"
                   "When a single source is given, <dest> may be a new file path."
  };
  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc < 2) {
    (void)fprintf(stderr, "cp: missing operand\n");
    return 1;
  }

  const char *dst  = rest.argv[rest.argc - 1];
  int         nsrc = rest.argc - 1;

  struct stat st;
  int         dst_is_dir = (stat(dst, &st) == 0 && S_ISDIR(st.st_mode));

  if(!dst_is_dir && nsrc > 1) {
    (void)fprintf(stderr, "cp: target '%s' is not a directory\n", dst);
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < nsrc; i++) {
    const char *src = rest.argv[i];

    if(dst_is_dir) {
      char path[4096];
      if(build_dest_path(path, sizeof path, dst, src)) {
        (void)fprintf(stderr, "cp: path too long for '%s'\n", src);
        exit_code = 1;
        continue;
      }
      if(copy_file(src, path))
        exit_code = 1;
    } else {
      if(copy_file(src, dst))
        exit_code = 1;
    }
  }
  return exit_code;
}
