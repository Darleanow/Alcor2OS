/**
 * Alcor2 Shell - Command Parser
 */

#include "shell.h"

/**
 * Parse a command line into a command structure.
 *
 * @param line  The input line (will be modified in place)
 * @param cmd   The output command structure
 * @return      Number of arguments parsed, or -1 on error
 */
int parse_command(char *line, command_t *cmd)
{
  cmd->cmd  = (char *)0;
  cmd->argc = 0;

  for(int i = 0; i < MAX_ARGS; i++) {
    cmd->args[i] = (char *)0;
  }

  /* Skip leading whitespace */
  while(*line == ' ' || *line == '\t') {
    line++;
  }

  /* Empty line */
  if(*line == '\0') {
    return 0;
  }

  /* Extract command name */
  cmd->cmd = line;

  while(*line && *line != ' ' && *line != '\t') {
    line++;
  }

  /* Null-terminate command if there are arguments */
  if(*line) {
    *line++ = '\0';
  }

  /* Parse arguments */
  while(*line && cmd->argc < MAX_ARGS - 1) {
    /* Skip whitespace */
    while(*line == ' ' || *line == '\t') {
      line++;
    }

    if(*line == '\0') {
      break;
    }

    /* Handle quoted strings */
    if(*line == '"' || *line == '\'') {
      char quote             = *line++;
      cmd->args[cmd->argc++] = line;

      while(*line && *line != quote) {
        line++;
      }

      if(*line) {
        *line++ = '\0';
      }
    } else {
      /* Regular argument */
      cmd->args[cmd->argc++] = line;

      while(*line && *line != ' ' && *line != '\t') {
        line++;
      }

      if(*line) {
        *line++ = '\0';
      }
    }
  }

  /* NULL terminate args array */
  cmd->args[cmd->argc] = (char *)0;

  return cmd->argc;
}
