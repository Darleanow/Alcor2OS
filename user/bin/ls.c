/**
 * @file user/bin/ls.c
 * @brief List directory contents.
 */

#include <dirent.h>
#include <grendizer.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @brief ls main entry point.
 */
int main(int argc, char *argv[])
{
  int    show_all = 0;
  gr_opt opts[]   = {
      GR_FLAG('a', "all", &show_all, "Do not ignore entries starting with ."),
      GR_END
  };

  gr_spec spec =
      {.program = "ls", .usage = "[options] [path]", .options = opts};

  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  const char *path = (rest.argc > 0) ? rest.argv[0] : ".";

  DIR        *dir = opendir(path);
  if(!dir) {
    (void)fprintf(stderr, "ls: cannot access '%s': No such directory\n", path);
    return 1;
  }

  const struct dirent *entry;
  while((entry = readdir(dir)) != NULL) {
    /* Skip hidden files unless -a is set */
    if(!show_all && entry->d_name[0] == '.')
      continue;

    if(entry->d_type == DT_DIR) {
      printf("\033[1;34m[DIR]  \033[0m");
    } else {
      printf("\033[0;32m[FILE] \033[0m");
    }
    printf("%s\n", entry->d_name);
  }

  closedir(dir);
  return 0;
}
