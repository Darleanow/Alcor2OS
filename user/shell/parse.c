/**
 * @file user/shell/parse.c
 * @brief Recursive-descent parser building vega AST nodes from tokens.
 *
 * Phase 1 grammar:
 *
 *   script  := command EOF
 *   command := (WORD | STRING)+
 *
 * Operators, redirections and pipes are recognised by the lexer but rejected
 * here with a clear error. Later phases lift these restrictions.
 */

#include "parse.h"
#include "lexer.h"
#include "shell.h"
#include <stdlib.h>

static void diag_unexpected(tok_kind_t k)
{
  sh_puts("vega: syntax error near ");
  sh_puts(tok_name(k));
  sh_puts("\n");
}

static ast_t *parse_command(lexer_t *L)
{
  ast_t *n = ast_new_cmd();
  if(!n)
    return NULL;

  for(;;) {
    tok_t t = lex_peek(L);
    if(t.kind == TOK_WORD || t.kind == TOK_STRING) {
      lex_next(L);
      if(ast_cmd_push_arg(n, t.text) < 0) {
        free(t.text);
        ast_free(n);
        return NULL;
      }
    } else {
      break;
    }
  }

  if(n->u.cmd.argc == 0) {
    ast_free(n);
    return NULL;
  }
  return n;
}

ast_t *vega_parse(const char *line)
{
  lexer_t L;
  lex_init(&L, line);

  ast_t *root = parse_command(&L);
  if(L.error) {
    ast_free(root);
    return NULL;
  }

  tok_t t = lex_next(&L);
  if(t.kind != TOK_EOF) {
    diag_unexpected(t.kind);
    lex_token_free(&t);
    ast_free(root);
    return NULL;
  }
  return root;
}
