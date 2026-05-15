/**
 * @file apps/shell/platform/builtins.c
 * @brief Shell-UX builtins — only meaningful when vega is running interactively
 * inside a terminal shell. Hook into libvega via sh_is_builtin / sh_run_builtin
 * (called by exec.c after its own builtin table).
 */

#include <shell/shell.h>
#include <stdlib.h>
#include <vega/host.h>
#include <vega/vega.h>

static const char *shell_builtins[] = {"help", "version", "clear", "kbd", NULL};

static void        cmd_help(void)
{
  sh_puts("\n");
  sh_puts("  vega - Command Reference\n");
  sh_puts("\n");

  sh_puts("  Language builtins:\n");
  sh_puts("    exit              Exit the shell\n");
  sh_puts("    cd <dir>          Change directory\n");
  sh_puts("    pwd               Print working directory\n");
  sh_puts("    let NAME VALUE    Set a vega variable (read with $NAME)\n");
  sh_puts("\n");

  sh_puts("  Shell builtins:\n");
  sh_puts("    help              Show this help message\n");
  sh_puts("    version           Show OS + vega version\n");
  sh_puts("    clear             Clear the screen\n");
  sh_puts("    kbd us|fr         Set PS/2 keymap layout (US QWERTY or FR "
          "AZERTY-ish)\n");
  sh_puts("\n");

  sh_puts("  External Commands (/bin):\n");
  sh_puts("    ls [dir]          List directory contents\n");
  sh_puts("    cat <file>        Display file contents\n");
  sh_puts("    mkdir <dir>       Create directory\n");
  sh_puts("    touch <file>      Create empty file\n");
  sh_puts("    rm <file>         Remove file\n");
  sh_puts("    vega [-c | FILE]  Standalone vega interpreter\n");
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

bool sh_is_builtin(const char *name)
{
  for(int i = 0; shell_builtins[i]; i++) {
    if(sh_strcmp(name, shell_builtins[i]) == 0)
      return true;
  }
  return false;
}

int sh_run_builtin(int argc, char *const argv[])
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
  if(sh_strcmp(name, "kbd") == 0)
    return cmd_kbd(argc, argv);

  return -1;
}
