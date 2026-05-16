/**
 * @file sdk/vega/vega.c
 * @brief vega top-level: host registration + lex → parse → execute → free.
 */

#include <vega/ast.h>
#include <vega/host.h>
#include <vega/internal/exec.h>
#include <vega/internal/host.h>
#include <vega/parse.h>
#include <vega/vega.h>

const vega_host_ops_t *vega_host = NULL;

void vega_init(const vega_host_ops_t *ops)
{
  vega_host = ops;
}

int vega_run(const char *line)
{
  ast_t *ast = vega_parse(line);
  if(!ast)
    return 0;
  int status = vega_exec(ast);
  ast_free(ast);
  return status;
}
