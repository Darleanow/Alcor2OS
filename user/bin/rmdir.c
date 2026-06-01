/**
 * @file user/bin/rmdir.c
 * @brief Remove empty directories.
 */

#include <errno.h>
#include <grendizer.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Remove a single empty directory, printing an error on failure.
 *
 * @param path    Directory path to remove.
 * @param verbose Non-zero to print a message before attempting removal.
 * @return 0 on success, -1 on failure.
 */
static int remove_dir(const char *path, int verbose)
{
  if(verbose)
    (void)fprintf(stdout, "rmdir: removing directory '%s'\n", path);
  if(rmdir(path) < 0) {
    (void)fprintf(stderr, "rmdir: failed to remove '%s': %s\n", path,
                  strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * @brief Remove a directory and optionally each successive parent component.
 *
 * @param path    Directory path to remove.
 * @param parents Non-zero to also remove ancestor directories.
 * @param verbose Non-zero to print a message before each removal attempt.
 * @return 0 on success, -1 on the first failure.
 */
static int remove_with_parents(const char *path, int parents, int verbose)
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

  if(remove_dir(buf, verbose) < 0)
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

    if(remove_dir(buf, verbose) < 0)
      return -1;
  }

  return 0;
}

/**
 * @brief Entry point for the @c rmdir utility.
 *
 * Parses command-line flags (-p, -v) and removes each specified directory.
 * With @c -p, ancestor components are removed after the leaf.  Exits with
 * status 0 only if every removal succeeded.
 *
 * @param argc Argument count from the shell.
 * @param argv Argument vector from the shell.
 * @return 0 on success, 1 if any directory could not be removed.
 */
int main(int argc, char *argv[])
{
  int parents = 0;
  int verbose = 0;

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
      GR_END
  };

  gr_spec spec = {
      .program = "rmdir", .usage = "[options] <dir> [...]", .options = opts
  };

  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(rest.argc == 0) {
    (void)fprintf(stderr,
                  "rmdir: missing operand\n"
                  "Try 'rmdir --help' for more information.\n");
    return 1;
  }

  int exit_code = 0;
  for(int i = 0; i < rest.argc; i++) {
    if(remove_with_parents(rest.argv[i], parents, verbose) < 0)
      exit_code = 1;
  }
  return exit_code;
}
