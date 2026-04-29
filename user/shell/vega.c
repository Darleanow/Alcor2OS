/**
 * @file user/shell/vega.c
 * @brief vega top-level: lex → parse → execute → free.
 */

#include "vega.h"
#include "ast.h"
#include "exec.h"
#include "parse.h"

void vega_run(const char *line)
{
  ast_t *ast = vega_parse(line);
  if(!ast)
    return;
  vega_exec(ast);
  ast_free(ast);
}
