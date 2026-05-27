/**
 * @file platform/complete.c
 * @brief Tab-completion engine for the vega shell.
 *
 * Provides command completion (builtins + executables in /init, /bin, /usr/bin)
 * and path completion (directory entries matching a prefix). Results include
 * the longest common prefix for single-tab insertion and the full candidate
 * list for double-tab display.
 */

#include <shell/shell.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Add a candidate to @p out if it matches @p prefix and isn't a
 * duplicate.
 *
 * @param out    Result accumulator.
 * @param name   Candidate name.
 * @param prefix Prefix to match against.
 * @param plen   Length of @p prefix.
 */
static void try_add(
    comp_result_t *out, const char *name, const char *prefix, size_t plen
)
{
  if(strncmp(name, prefix, plen) != 0)
    return;
  if(out->count >= COMP_MAX)
    return;
  for(int i = 0; i < out->count; i++) {
    if(strcmp(out->entries[i], name) == 0)
      return;
  }
  strncpy(out->entries[out->count], name, COMP_NAME_MAX - 1);
  out->entries[out->count][COMP_NAME_MAX - 1] = '\0';
  out->count++;
}

/**
 * @brief Scan a directory for entries matching @p prefix and add them.
 *
 * @param out    Result accumulator.
 * @param dir    Directory path to scan.
 * @param prefix Filename prefix to match.
 * @param plen   Length of @p prefix.
 */
static void scan_dir(
    comp_result_t *out, const char *dir, const char *prefix, size_t plen
)
{
  DIR *d = sh_opendir(dir);
  if(!d)
    return;
  struct dirent *ent;
  while((ent = sh_readdir(d)) != NULL) {
    if(ent->d_name[0] == '.' && prefix[0] != '.')
      continue;
    try_add(out, ent->d_name, prefix, plen);
  }
  sh_closedir(d);
}

/**
 * @brief Compute the longest common prefix across all entries in @p out.
 *
 * @param out  Result set; out->common is filled on return.
 */
static void compute_common(comp_result_t *out)
{
  out->common[0] = '\0';
  if(out->count == 0)
    return;
  strncpy(out->common, out->entries[0], COMP_NAME_MAX - 1);
  out->common[COMP_NAME_MAX - 1] = '\0';
  for(int i = 1; i < out->count; i++) {
    int j = 0;
    while(out->common[j] && out->common[j] == out->entries[i][j])
      j++;
    out->common[j] = '\0';
  }
}

/**
 * @brief Compute tab-completion candidates for @p prefix.
 *
 * When @p is_command is true, matches against shell builtins and executables
 * in @c /init, @c /bin, @c /usr/bin. Otherwise matches filesystem entries
 * under the directory implied by @p prefix (or CWD when @p prefix has no
 * slash). Directories get a trailing @c / in their entry name.
 *
 * @param prefix      Partial word typed so far.
 * @param is_command  true for command-position completion (first word on
 *                    the line), false for path completion.
 * @param out         Result accumulator; zeroed on entry, filled with
 *                    candidates and the longest common prefix on return.
 */
void sh_complete(const char *prefix, bool is_command, comp_result_t *out)
{
  memset(out, 0, sizeof(*out));
  size_t plen = strlen(prefix);

  if(is_command) {
    const char *const *builtins = sh_builtin_list();
    for(int i = 0; builtins[i]; i++)
      try_add(out, builtins[i], prefix, plen);

    static const char *const cmd_dirs[] = {"/init", "/bin", "/usr/bin", NULL};
    for(int d = 0; cmd_dirs[d]; d++)
      scan_dir(out, cmd_dirs[d], prefix, plen);
  } else {
    const char *slash = strrchr(prefix, '/');
    if(slash) {
      char   dir[MAX_PATH];
      size_t dlen = (size_t)(slash - prefix);
      if(dlen == 0) {
        dir[0] = '/';
        dir[1] = '\0';
      } else if(dlen < sizeof(dir)) {
        memcpy(dir, prefix, dlen);
        dir[dlen] = '\0';
      } else {
        return;
      }
      const char *name_prefix = slash + 1;
      size_t      nplen       = strlen(name_prefix);
      /* dir_prefix is everything up to and including the last '/'. */
      char dir_prefix[MAX_PATH];
      (void)snprintf(
          dir_prefix, sizeof(dir_prefix), "%.*s/", (int)dlen, prefix
      );

      DIR *d = sh_opendir(dir);
      if(!d)
        return;
      struct dirent *ent;
      while((ent = sh_readdir(d)) != NULL) {
        if(ent->d_name[0] == '.' && name_prefix[0] != '.')
          continue;
        if(strncmp(ent->d_name, name_prefix, nplen) != 0)
          continue;
        if(out->count >= COMP_MAX)
          break;
        /* Entries store dir_prefix + name so the common prefix includes
         * the directory portion and the insertion math in read_line
         * (common_len - word_len = chars to insert) works correctly. */
        char full_stat[MAX_PATH];
        if(dlen == 1 && dir[0] == '/') {
          (void)snprintf(full_stat, sizeof(full_stat), "/%s", ent->d_name);
        } else {
          (void)snprintf(
              full_stat, sizeof(full_stat), "%s/%s", dir, ent->d_name
          );
        }
        char        entry[COMP_NAME_MAX];
        struct stat st;
        if(sh_stat(full_stat, &st) == 0 && S_ISDIR(st.st_mode)) {
          (void)snprintf(
              entry, sizeof(entry), "%s%s/", dir_prefix, ent->d_name
          );
        } else {
          (void)snprintf(entry, sizeof(entry), "%s%s", dir_prefix, ent->d_name);
        }
        try_add(out, entry, prefix, plen);
      }
      sh_closedir(d);
    } else {
      char cwd[MAX_PATH];
      if(!sh_getcwd(cwd, sizeof(cwd)))
        return;
      DIR *d = sh_opendir(cwd);
      if(!d)
        return;
      struct dirent *ent;
      while((ent = sh_readdir(d)) != NULL) {
        if(ent->d_name[0] == '.' && prefix[0] != '.')
          continue;
        if(strncmp(ent->d_name, prefix, plen) != 0)
          continue;
        if(out->count >= COMP_MAX)
          break;

        char full[MAX_PATH];
        (void)snprintf(full, sizeof(full), "%s/%s", cwd, ent->d_name);
        struct stat st;
        if(sh_stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
          char with_slash[COMP_NAME_MAX];
          (void)snprintf(with_slash, sizeof(with_slash), "%s/", ent->d_name);
          try_add(out, with_slash, prefix, plen);
        } else {
          try_add(out, ent->d_name, prefix, plen);
        }
      }
      sh_closedir(d);
    }
  }

  compute_common(out);
}
