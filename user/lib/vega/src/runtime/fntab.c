/**
 * @file user/shell/fntab.c
 * @brief Fixed-capacity function table.
 */

#include <stdlib.h>
#include <vega/host.h>
#include <vega/runtime/fntab.h>

#define MAX_FUNCTIONS 16

static fn_entry_t  g_table[MAX_FUNCTIONS];
static int         g_count = 0;

static fn_entry_t *find_slot(const char *name)
{
  for(int i = 0; i < g_count; i++) {
    if(sh_strcmp(g_table[i].name, name) == 0)
      return &g_table[i];
  }
  return NULL;
}

static void free_entry(fn_entry_t *e)
{
  free(e->name);
  if(e->arg_names) {
    for(int i = 0; i < e->n_args; i++)
      free(e->arg_names[i]);
    free(e->arg_names);
  }
  ast_free(e->body);
  e->name      = NULL;
  e->arg_names = NULL;
  e->n_args    = 0;
  e->body      = NULL;
}

int fntab_set(char *name, char **arg_names, int n_args, ast_t *body)
{
  fn_entry_t *slot = find_slot(name);
  if(slot) {
    free_entry(slot);
    slot->name      = name;
    slot->arg_names = arg_names;
    slot->n_args    = n_args;
    slot->body      = body;
    return 0;
  }
  if(g_count >= MAX_FUNCTIONS)
    return -1;
  slot            = &g_table[g_count++];
  slot->name      = name;
  slot->arg_names = arg_names;
  slot->n_args    = n_args;
  slot->body      = body;
  return 0;
}

const fn_entry_t *fntab_get(const char *name)
{
  return find_slot(name);
}
