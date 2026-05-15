/**
 * @file sdk/vega/builtin.c
 * @brief Language builtins — meaningful in any vega context (scripts, embeds,
 * the interactive shell). Shell-UX builtins (help, version, clear, kbd) live
 * in apps/shell and plug in via sh_is_builtin / sh_run_builtin host hooks.
 */

#include <stdlib.h>
#include <vega/host.h>
#include <vega/vega.h>

#include "builtin.h"
#include "expand.h"

static const char *builtins[] = {"exit", "cd", "pwd", "let", NULL};

static void        cmd_exit(void)
{
  exit(0);
}

static int cmd_cd(int argc, char *const argv[])
{
  const char *path = (argc > 1) ? argv[1] : "/";

  if(sh_chdir(path) < 0) {
    sh_puts("cd: ");
    sh_puts(path);
    sh_puts(": No such directory\n");
    return 1;
  }
  return 0;
}

static int cmd_pwd(void)
{
  char cwd[MAX_PATH];

  if(sh_getcwd(cwd, sizeof(cwd)) != NULL) {
    sh_puts(cwd);
    sh_putchar('\n');
    return 0;
  }
  sh_puts("pwd: error\n");
  return 1;
}

/* Set a vega variable: `let NAME VALUE`. The value is a single arg, so use
 * quoting if it contains spaces: `let greeting "hello world"`. */
static int cmd_let(int argc, char *const argv[])
{
  if(argc < 3) {
    sh_puts("usage: let NAME VALUE\n");
    return 1;
  }
  if(expand_setvar(argv[1], argv[2]) < 0) {
    sh_puts("let: failed to set variable\n");
    return 1;
  }
  return 0;
}

int is_builtin(const char *cmd)
{
  for(int i = 0; builtins[i]; i++) {
    if(sh_strcmp(cmd, builtins[i]) == 0)
      return 1;
  }
  return 0;
}

int run_builtin(int argc, char *const argv[])
{
  const char *name = argv[0];

  if(sh_strcmp(name, "exit") == 0) {
    cmd_exit();
    return 0;
  }
  if(sh_strcmp(name, "cd") == 0)
    return cmd_cd(argc, argv);
  if(sh_strcmp(name, "pwd") == 0)
    return cmd_pwd();
  if(sh_strcmp(name, "let") == 0)
    return cmd_let(argc, argv);

  return -1;
}
