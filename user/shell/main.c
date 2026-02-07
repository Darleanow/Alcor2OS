/**
 * Alcor2 Shell - Main Entry Point
 *
 * A clean, modular shell for Alcor2 OS.
 */

#include "shell.h"
#include <stddef.h>
#include <stdlib.h>

/**
 * @brief Run an external command from /bin directory.
 * @param cmd Command structure with command name and arguments.
 * @return 0 on success, -1 on failure.
 */
static int run_external(command_t *cmd)
{
  char        path[MAX_PATH];
  char       *p      = path;
  const char *prefix = "/bin/";

  while(*prefix) {
    *p++ = *prefix++;
  }

  const char *c = cmd->cmd;
  while(*c && p < path + MAX_PATH - 1) {
    *p++ = *c++;
  }
  *p = '\0';

  int ret = sh_exec(path, cmd->args);
  return (ret < 0) ? -1 : 0;
}

/**
 * @brief Parse and execute a command line.
 * @param line Command line to execute.
 */
static void execute(char *line)
{
  command_t cmd;

  if(parse_command(line, &cmd) < 0) {
    return;
  }

  if(!cmd.cmd) {
    return;
  }

  if(is_builtin(cmd.cmd)) {
    run_builtin(&cmd);
    return;
  }

  if(run_external(&cmd) < 0) {
    sh_puts(cmd.cmd);
    sh_puts(": command not found\n");
  }
}

/**
 * @brief Read a line of input from the user with basic line editing.
 * @param buf Buffer to store the line.
 * @param size Size of the buffer.
 * @return Number of characters read, or special value for EOF.
 */
static int read_line(char *buf, size_t size)
{
  size_t pos = 0;
  int    c;

  while(1) {
    c = sh_getchar();
    if(c < 0)
      continue;

    if(c == '\n' || c == '\r') {
      sh_putchar('\n');
      buf[pos] = '\0';
      return (int)pos;
    }

    if(c == '\b' || c == 127) {
      if(pos > 0) {
        pos--;
        sh_puts("\b \b");
      }
      continue;
    }

    if(c == 0x03) {
      sh_puts("^C\n");
      buf[0] = '\0';
      return 0;
    }

    if(c == 0x04) {
      if(pos == 0) {
        return -1;
      }
      continue;
    }

    if(c == 0x0C) {
      sh_clear();
      return 0;
    }

    if(c >= 32 && c < 127 && pos < size - 1) {
      buf[pos++] = (char)c;
      sh_putchar((char)c);
    }
  }
}

/**
 * @brief Print the shell prompt with current working directory.
 */
static void print_prompt(void)
{
  char cwd[MAX_PATH];

  if(sh_getcwd(cwd, sizeof(cwd)) != NULL) {
    sh_puts("alcor2:");
    sh_puts(cwd);
    sh_puts("$ ");
  } else {
    sh_puts("alcor2> ");
  }
}

/**
 * @brief Shell main entry point.
 *
 * Displays welcome message and enters the main read-eval-print loop.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code (0 on normal exit).
 */
int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  char line[MAX_CMD_LEN];

  sh_puts("\n");
  sh_puts("  Welcome to Alcor2 Shell!\n");
  sh_puts("  Type 'help' for available commands.\n");
  sh_puts("\n");

  while(1) {
    print_prompt();

    int len = read_line(line, sizeof(line));

    if(len < 0) {
      sh_puts("exit\n");
      exit(0);
    }

    if(len > 0) {
      execute(line);
    }
  }

  return 0;
}
