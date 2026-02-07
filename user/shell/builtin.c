/**
 * Alcor2 Shell - Builtin Commands
 *
 * Commands that run in the shell process itself.
 */

#include "shell.h"
#include <stdlib.h>

/**
 * @brief Table of builtin command names.
 */
static const char *builtins[] = {"help", "version", "clear", "exit",
                                 "cd",   "pwd",     NULL};

/**
 * @name Builtin command implementations
 * @{
 */

/**
 * @brief Display shell help message with builtin and external commands.
 */
static void cmd_help(void)
{
  sh_puts("\n");
  sh_puts("  Alcor2 Shell - Command Reference\n");
  sh_puts("  ================================\n\n");

  sh_puts("  Builtin Commands:\n");
  sh_puts("    help              Show this help message\n");
  sh_puts("    version           Show OS version\n");
  sh_puts("    clear             Clear the screen\n");
  sh_puts("    exit              Exit the shell\n");
  sh_puts("    echo [text...]    Display text\n");
  sh_puts("    cd <dir>          Change directory\n");
  sh_puts("    pwd               Print working directory\n");
  sh_puts("\n");

  sh_puts("  External Commands (/bin):\n");
  sh_puts("    ls [dir]          List directory contents\n");
  sh_puts("    cat <file>        Display file contents\n");
  sh_puts("    mkdir <dir>       Create directory\n");
  sh_puts("    touch <file>      Create empty file\n");
  sh_puts("    rm <file>         Remove file\n");
  sh_puts("\n");
}

/**
 * @brief Display OS and shell version information.
 */
static void cmd_version(void)
{
  sh_puts("Alcor2 Operating System v0.1.0\n");
  sh_puts("Shell version ");
  sh_puts(SHELL_VERSION);
  sh_puts("\n");
}

/**
 * @brief Clear the terminal screen.
 */
static void cmd_clear(void)
{
  sh_clear();
}

/**
 * @brief Exit the shell and return to init process.
 */
static void cmd_exit(void)
{
  sh_puts("Goodbye!\n");
  exit(0);
}

/**
 * @brief Change current working directory.
 * @param cmd Command structure with directory path as first argument.
 */
static void cmd_cd(command_t *cmd)
{
  const char *path = cmd->args[0];

  if(!path) {
    path = "/";
  }

  if(sh_chdir(path) < 0) {
    sh_puts("cd: ");
    sh_puts(path);
    sh_puts(": No such directory\n");
  }
}

/**
 * @brief Print current working directory.
 */
static void cmd_pwd(void)
{
  char cwd[MAX_PATH];

  if(sh_getcwd(cwd, sizeof(cwd)) != NULL) {
    sh_puts(cwd);
    sh_putchar('\n');
  } else {
    sh_puts("pwd: error\n");
  }
}
/** @} */

/**
 * @brief Check if a command name is a builtin.
 * @param cmd Command name to check.
 * @return 1 if builtin, 0 otherwise.
 */
int is_builtin(const char *cmd)
{
  for(int i = 0; builtins[i]; i++) {
    if(sh_strcmp(cmd, builtins[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/**
 * @brief Execute a builtin command.
 * @param cmd Command structure with command name and arguments.
 * @return 0 on success, -1 if command not found.
 */
int run_builtin(command_t *cmd)
{
  if(sh_strcmp(cmd->cmd, "help") == 0) {
    cmd_help();
  } else if(sh_strcmp(cmd->cmd, "version") == 0) {
    cmd_version();
  } else if(sh_strcmp(cmd->cmd, "clear") == 0) {
    cmd_clear();
  } else if(sh_strcmp(cmd->cmd, "exit") == 0) {
    cmd_exit();
  } else if(sh_strcmp(cmd->cmd, "cd") == 0) {
    cmd_cd(cmd);
  } else if(sh_strcmp(cmd->cmd, "pwd") == 0) {
    cmd_pwd();
  } else {
    return -1;
  }

  return 0;
}
