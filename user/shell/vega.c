/**
 * @file user/shell/vega.c
 * @brief vega top-level dispatcher: takes a line, runs builtin or external.
 *
 * Phase 0 wires the entry point. Subsequent phases grow this module into a
 * full lexer/parser/executor pipeline.
 */

#include "vega.h"
#include "shell.h"

/**
 * @brief Try to run @p cmd as an external program from /bin or /usr/bin.
 *
 * Absolute paths are executed directly. Otherwise the command name is looked
 * up in a fixed search list.
 *
 * @return 0 on success, -1 if the binary could not be executed from any path.
 */
static int run_external(command_t *cmd)
{
  char  path[MAX_PATH];
  char *p = path;

  if(cmd->cmd[0] == '/') {
    const char *c = cmd->cmd;
    while(*c && p < path + MAX_PATH - 1)
      *p++ = *c++;
    *p      = '\0';
    int ret = sh_exec(path, cmd->args);
    return (ret < 0) ? -1 : 0;
  }

  static const char *const dirs[] = { "/bin/", "/usr/bin/", NULL };
  for(int i = 0; dirs[i]; i++) {
    p                  = path;
    const char *prefix = dirs[i];
    while(*prefix)
      *p++ = *prefix++;
    const char *c = cmd->cmd;
    while(*c && p < path + MAX_PATH - 1)
      *p++ = *c++;
    *p      = '\0';
    int ret = sh_exec(path, cmd->args);
    if(ret >= 0)
      return 0;
  }
  return -1;
}

void vega_run(char *line)
{
  command_t cmd;

  if(parse_command(line, &cmd) < 0)
    return;

  if(!cmd.cmd)
    return;

  if(is_builtin(cmd.cmd)) {
    run_builtin(&cmd);
    return;
  }

  if(run_external(&cmd) < 0) {
    sh_puts(cmd.cmd);
    sh_puts(": command not found\n");
  }
}
