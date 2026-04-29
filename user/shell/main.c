/**
 * @file user/shell/main.c
 * @brief vega REPL: read a line, hand it to vega_run().
 */

#include "shell.h"
#include "vega.h"
#include <stddef.h>
#include <stdlib.h>

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

    /* ISO 8859-1 (same bytes as Unicode U+0080-U+00FF): allow printable high bytes after user sets `kbd fr`. */
    unsigned char uc = (unsigned char)c;
    if(((uc >= 32u && uc < 127u) || uc >= 160u) && pos < size - 1) {
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
  sh_puts("  vega " VEGA_VERSION " - Alcor2 shell\n");
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
      vega_run(line);
    }
  }

  return 0;
}
