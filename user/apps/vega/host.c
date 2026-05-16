/**
 * @file apps/vega/host.c
 * @brief vega CLI host implementation.
 *
 * The CLI has no host-side builtins (cd/pwd/etc. live in the shell). Plain
 * stubs return false / -1 so libvega falls through to the external command
 * path for every command.
 */

#include <stdbool.h>
#include <vega/host.h>

static bool cli_is_builtin(const char *name)
{
  (void)name;
  return false;
}

static int cli_run_builtin(int argc, char *const argv[])
{
  (void)argc;
  (void)argv;
  return -1;
}

const vega_host_ops_t vega_cli_host = {
    .is_builtin  = cli_is_builtin,
    .run_builtin = cli_run_builtin,
};
