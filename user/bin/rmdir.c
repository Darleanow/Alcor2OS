/**
 * @file user/bin/rmdir.c
 * @brief Remove empty directories.
 */

#include <dirent.h>
#include <errno.h>
#include <grendizer.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Remove a single empty directory, printing a diagnostic on failure.
 *
 * @param path     Directory path to remove.
 * @param verbose  Non-zero to print a message before attempting removal.
 * @param simulate Non-zero to print the action without executing it.
 * @return 0 on success, -1 on failure.
 */
static int remove_dir(const char *path, int verbose, int simulate)
{
  if(verbose || simulate)
    (void)fprintf(
        stdout,
        simulate ? "rmdir: [dry-run] removing directory '%s'\n"
                 : "rmdir: removing directory '%s'\n",
        path
    );
  if(simulate)
    return 0;
  if(rmdir(path) < 0) {
    (void)fprintf(
        stderr, "rmdir: failed to remove '%s': %s\n", path, strerror(errno)
    );
    return -1;
  }
  return 0;
}

/**
 * @brief Recursively remove all content of a directory, then the
 *        directory itself.
 *
 * Uses @c d_type to distinguish files from subdirectories without a @c stat
 * call, consistent with the rest of the userland tools.
 *
 * @param path     Directory path to clear and remove.
 * @param verbose  Non-zero to print a message before each removal.
 * @param simulate Non-zero to print actions without executing them.
 * @return 0 on success, -1 on the first failure.
 */
static int remove_recursive(const char *path, int verbose, int simulate)
{
  DIR *dir = opendir(path);
  if(!dir) {
    (void)fprintf(
        stderr, "rmdir: cannot open '%s': %s\n", path, strerror(errno)
    );
    return -1;
  }

  const struct dirent *de;
  int                  rc = 0;
  char                 child[PATH_MAX];

  while((de = readdir(dir)) != NULL) {
    if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    int n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
    if(n < 0 || n >= (int)sizeof(child)) {
      (void)fprintf(stderr, "rmdir: path too long\n");
      rc = -1;
      continue;
    }

    if(de->d_type == DT_DIR) {
      if(remove_recursive(child, verbose, simulate) < 0)
        rc = -1;
    } else {
      if(verbose || simulate)
        (void)fprintf(
            stdout,
            simulate ? "rmdir: [dry-run] removing '%s'\n"
                     : "rmdir: removing '%s'\n",
            child
        );
      if(!simulate && unlink(child) < 0) {
        (void)fprintf(
            stderr, "rmdir: cannot remove '%s': %s\n", child, strerror(errno)
        );
        rc = -1;
      }
    }
  }
  closedir(dir);

  if(rc == 0)
    return remove_dir(path, verbose, simulate);
  return -1;
}

/**
 * @brief Remove a directory and optionally its content and ancestor components.
 *
 * When @p parents is set, ancestor directories are removed after the leaf;
 * a non-empty ancestor (ENOTEMPTY / EEXIST) stops the walk without error.
 * When @p content is set, the directory tree is removed recursively.
 *
 * @param path     Directory path to remove.
 * @param parents  Non-zero to also remove ancestor directories.
 * @param content  Non-zero to recursively remove directory content first.
 * @param verbose  Non-zero to print a message before each removal attempt.
 * @param simulate Non-zero to print actions without executing them.
 * @return 0 on success, -1 on the first fatal failure.
 */
static int remove_with_parents(
    const char *path, int parents, int content, int verbose, int simulate
)
{
  char buf[PATH_MAX];
  int  len = (int)strlen(path);

  if(len == 0) {
    (void)fprintf(stderr, "rmdir: empty path\n");
    return -1;
  }
  if(len >= (int)sizeof(buf)) {
    (void)fprintf(stderr, "rmdir: path too long\n");
    return -1;
  }

  memcpy(buf, path, (size_t)(len + 1));

  /* Strip trailing slashes, except for root. */
  while(len > 1 && buf[len - 1] == '/')
    buf[--len] = '\0';

  int ret = content ? remove_recursive(buf, verbose, simulate)
                    : remove_dir(buf, verbose, simulate);
  if(ret < 0)
    return -1;

  if(!parents)
    return 0;

  while(len > 1) {
    int i = len - 1;
    while(i > 0 && buf[i] != '/')
      i--;

    if(i <= 0)
      break;

    buf[i] = '\0';
    len    = i;

    if(verbose || simulate)
      (void)fprintf(
          stdout,
          simulate ? "rmdir: [dry-run] removing directory '%s'\n"
                   : "rmdir: removing directory '%s'\n",
          buf
      );
    if(!simulate) {
      if(rmdir(buf) < 0) {
        /* Parent still has other children: stop silently, not an error. */
        if(errno == ENOTEMPTY || errno == EEXIST)
          break;
        (void)fprintf(
            stderr, "rmdir: failed to remove '%s': %s\n", buf, strerror(errno)
        );
        return -1;
      }
    }
  }

  return 0;
}

/**
 * @brief Entry point for the @c rmdir utility.
 *
 * Parses command-line flags (-p, -v, -n, -c) and removes each specified
 * directory.  With @c -p, ancestor components are removed after the leaf;
 * a non-empty ancestor stops the walk without error.  With @c -c, the
 * directory tree is removed recursively.  With @c -n, a preview of all
 * actions is shown and the user is asked to confirm before any deletion
 * is performed.  Exits with status 0 only if every leaf removal succeeded.
 *
 * @param argc Argument count from the shell.
 * @param argv Argument vector from the shell.
 * @return 0 on success, 1 if any directory could not be removed.
 */
int main(int argc, char *argv[])
{
  int parents = 0;
  int verbose = 0;
  int dry_run = 0;
  int content = 0;

  gr_opt opts[] = {
      GR_FLAG(
          'p',
          "parents",
          &parents,
          "remove directory and its ancestors (e.g. rmdir -p a/b/c removes "
          "a/b/c, a/b, then a)"
      ),
      GR_FLAG(
          'v',
          "verbose",
          &verbose,
          "print a diagnostic for each directory processed"
      ),
      GR_FLAG(
          'n',
          "dry-run",
          &dry_run,
          "show what would be removed and ask for confirmation before acting"
      ),
      GR_FLAG(
          'c',
          "content",
          &content,
          "recursively remove directory content before removing the directory "
          "itself"
      ),
      GR_END
  };

  gr_spec spec =
      {.program = "rmdir", .usage = "[options] <dir> [...]", .options = opts};

  gr_rest rest;

  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    (void)fprintf(
        stderr,
        "rmdir: missing operand\n"
        "Try 'rmdir --help' for more information.\n"
    );
    return 1;
  }

  if(dry_run) {
    /* Preview phase: show all actions that would be performed. */
    for(int i = 0; i < rest.argc; i++)
      (void)remove_with_parents(rest.argv[i], parents, content, verbose, 1);

    (void)fprintf(stdout, "Proceed? [y/N] ");
    (void)fflush(stdout);
    int ch = getchar();
    if(ch != 'y' && ch != 'Y')
      return 0;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    if(remove_with_parents(rest.argv[i], parents, content, verbose, 0) < 0)
      exit_code = 1;
  }
  return exit_code;
}
