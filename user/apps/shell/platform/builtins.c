/**
 * @file apps/shell/platform/builtins.c
 * @brief Shell-side builtins — implementations run in-process via libvega's
 * sh_is_builtin / sh_run_builtin host hooks. libvega itself has no builtin
 * commands; the language only knows expressions and control flow.
 */

#include <shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <vega/host.h>
#include <vega/vega.h>

static const char *shell_builtins[] = {
    "exit", "cd", "pwd", "let", "help", "version", "clear", NULL,
};

static void cmd_help(void)
{
  sh_puts("\n");
  sh_puts("  shell builtins:\n");
  sh_puts("    exit              quit the shell\n");
  sh_puts("    cd <dir>          change directory\n");
  sh_puts("    pwd               print working directory\n");
  sh_puts("    let NAME VALUE    set a vega variable (read with $NAME)\n");
  sh_puts("    help              this message\n");
  sh_puts("    version           OS + vega version\n");
  sh_puts("    clear             clear the screen\n");
  sh_puts("\n");
}

static void cmd_version(void)
{
  sh_puts("Alcor2 Operating System v0.1.0\n");
  sh_puts("vega ");
  sh_puts(VEGA_VERSION);
  sh_puts("\n");
}

static int cmd_cd(int argc, char *const argv[])
{
  const char *path = (argc > 1) ? argv[1] : "/";
  if(sh_chdir(path) < 0) {
    sh_puts("cd: ");
    sh_puts(path);
    sh_puts(": no such directory\n");
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

static int cmd_let(int argc, char *const argv[])
{
  if(argc < 3) {
    sh_puts("usage: let NAME VALUE\n");
    return 1;
  }
  if(vega_setvar(argv[1], argv[2]) < 0) {
    sh_puts("let: failed to set variable\n");
    return 1;
  }
  return 0;
}

bool sh_is_builtin(const char *name)
{
  for(int i = 0; shell_builtins[i]; i++) {
    if(strcmp(name, shell_builtins[i]) == 0)
      return true;
  }
  return false;
}

int sh_run_builtin(int argc, char *const argv[])
{
  const char *name = argv[0];

  if(strcmp(name, "exit") == 0)
    exit(0);
  if(strcmp(name, "cd") == 0)
    return cmd_cd(argc, argv);
  if(strcmp(name, "pwd") == 0)
    return cmd_pwd();
  if(strcmp(name, "let") == 0)
    return cmd_let(argc, argv);
  if(strcmp(name, "help") == 0) {
    cmd_help();
    return 0;
  }
  if(strcmp(name, "version") == 0) {
    cmd_version();
    return 0;
  }
  if(strcmp(name, "clear") == 0) {
    sh_clear();
    return 0;
  }
  return -1;
}
