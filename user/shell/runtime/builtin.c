/**
 * @file user/shell/builtin.c
 * @brief vega built-in commands. Receive POSIX-style argv (argv[0] is the
 * command name).
 */

#include <stdlib.h>
#include <vega/runtime/expand.h>
#include <vega/shell.h>
#include <vega/vega.h>

static const char *builtins[] = {"help", "version", "clear", "exit", "cd",
                                 "pwd",  "kbd",     "let",   NULL};

static void        cmd_help(void)
{
  sh_puts("\n");
  sh_puts("  vega - Command Reference\n");
  sh_puts("\n");

  sh_puts("  Builtin Commands:\n");
  sh_puts("    help              Show this help message\n");
  sh_puts("    version           Show OS version\n");
  sh_puts("    clear             Clear the screen\n");
  sh_puts("    exit              Exit the shell\n");
  sh_puts("    cd <dir>          Change directory\n");
  sh_puts("    pwd               Print working directory\n");
  sh_puts("    kbd us|fr         Set PS/2 keymap layout (US QWERTY or FR "
          "AZERTY-ish)\n");
  sh_puts("    let NAME VALUE    Set a vega variable (read with $NAME)\n");
  sh_puts("\n");

  sh_puts("  External Commands (/bin):\n");
  sh_puts("    ls [dir]          List directory contents\n");
  sh_puts("    cat <file>        Display file contents\n");
  sh_puts("    mkdir <dir>       Create directory\n");
  sh_puts("    touch <file>      Create empty file\n");
  sh_puts("    rm <file>         Remove file\n");
  sh_puts("    edi <file>        Tiny line editor (. alone = save)\n");
  sh_puts("\n");
}

static void cmd_version(void)
{
  sh_puts("Alcor2 Operating System v0.1.0\n");
  sh_puts("vega ");
  sh_puts(VEGA_VERSION);
  sh_puts("\n");
}

static void cmd_clear(void)
{
  sh_clear();
}

static void cmd_exit(void)
{
  sh_puts("Goodbye!\n");
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

/* Set keyboard layout (`us` | `fr`); semantics match kernel tty layer. */
static int cmd_kbd(int argc, char *const argv[])
{
  const char *what = (argc > 1) ? argv[1] : "us";
  if(sh_strcmp(what, "us") == 0) {
    sh_kbd_layout(KBD_LAYOUT_US);
    sh_puts("keyboard: layout us\n");
    return 0;
  }
  if(sh_strcmp(what, "fr") == 0) {
    sh_kbd_layout(KBD_LAYOUT_FR);
    sh_puts("keyboard: layout fr\n");
    return 0;
  }
  sh_puts("usage: kbd [us|fr]\n");
  return 1;
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

  if(sh_strcmp(name, "help") == 0) {
    cmd_help();
    return 0;
  }
  if(sh_strcmp(name, "version") == 0) {
    cmd_version();
    return 0;
  }
  if(sh_strcmp(name, "clear") == 0) {
    cmd_clear();
    return 0;
  }
  if(sh_strcmp(name, "exit") == 0) {
    cmd_exit();
    return 0;
  }
  if(sh_strcmp(name, "cd") == 0)
    return cmd_cd(argc, argv);
  if(sh_strcmp(name, "pwd") == 0)
    return cmd_pwd();
  if(sh_strcmp(name, "kbd") == 0)
    return cmd_kbd(argc, argv);
  if(sh_strcmp(name, "let") == 0)
    return cmd_let(argc, argv);

  return -1;
}
