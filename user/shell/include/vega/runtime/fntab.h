/**
 * @file user/shell/fntab.h
 * @brief vega user-defined function table.
 *
 * Stores registered functions keyed by name. Defining a function via the
 * `fn name(args) { body }` syntax routes through this module — it takes
 * ownership of the name, arg-names array and body AST. Redefining replaces
 * the previous body (the old AST is freed). The table is fixed-size for
 * simplicity; further entries are rejected.
 */

#ifndef VEGA_FNTAB_H
#define VEGA_FNTAB_H

#include <vega/frontend/ast.h>

typedef struct
{
  char  *name;
  char **arg_names; /* may be NULL when n_args == 0 */
  int    n_args;
  ast_t *body;      /* never NULL for an occupied entry */
} fn_entry_t;

/**
 * @brief Insert or replace a function. Takes ownership of @p name,
 * @p arg_names (and each string therein) and @p body.
 *
 * @return 0 on success, -1 if the table is full or allocation failed; on
 *         failure the caller still owns the inputs.
 */
int fntab_set(char *name, char **arg_names, int n_args, ast_t *body);

/**
 * @brief Look up a function by name.
 * @return Pointer to the entry (caller must not free), or NULL if absent.
 */
const fn_entry_t *fntab_get(const char *name);

#endif /* VEGA_FNTAB_H */
