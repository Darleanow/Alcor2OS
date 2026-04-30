/**
 * @file user/shell/parse.c
 * @brief Recursive-descent parser building vega AST nodes from tokens.
 *
 * Grammar:
 *
 *   script  := list EOF
 *   list    := and_or (SEMI and_or)*       -- empty separators ignored
 *   and_or  := command ((AND | OR) command)*
 *   command := (WORD | STRING)+
 *
 * Pipes, redirections and control-flow tokens are recognised by the lexer but
 * still rejected here; later phases lift those restrictions.
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

static void diag_expected_after(tok_kind_t after)
{
  sh_puts("vega: expected command after ");
  sh_puts(tok_name(after));
  sh_puts("\n");
}

static ast_t *parse_command(lexer_t *L)
{
  ast_t *n = ast_new_cmd();
  if(!n)
    return NULL;

  while(1) {
    tok_t t = lex_peek(L);
    if(t.kind != TOK_WORD && t.kind != TOK_STRING)
      break;
    lex_next(L);
    if(ast_cmd_push_arg(n, t.text) < 0) {
      free(t.text);
      ast_free(n);
      return NULL;
    }
  }

  if(n->u.cmd.argc == 0) {
    ast_free(n);
    return NULL;
  }
  return n;
}

static ast_t *parse_and_or(lexer_t *L)
{
  ast_t *left = parse_command(L);
  if(!left)
    return NULL;

  while(1) {
    tok_t t = lex_peek(L);
    if(t.kind != TOK_AND && t.kind != TOK_OR)
      break;
    lex_next(L);

    ast_t *right = parse_command(L);
    if(!right) {
      diag_expected_after(t.kind);
      ast_free(left);
      return NULL;
    }

    ast_kind_t k = (t.kind == TOK_AND) ? AST_AND : AST_OR;
    left         = ast_new_binop(k, left, right);
    if(!left)
      return NULL;
  }
  return left;
}

static ast_t *parse_list(lexer_t *L)
{
  ast_t *left = parse_and_or(L);
  if(L->error)
    return NULL;

  while(1) {
    tok_t t = lex_peek(L);
    if(t.kind != TOK_SEMI)
      break;
    lex_next(L); /* consume ';' */

    ast_t *right = parse_and_or(L);
    if(L->error) {
      ast_free(left);
      return NULL;
    }
    if(!right)
      continue; /* empty between/around separators is fine */

    if(!left) {
      left = right;
    } else {
      ast_t *combined = ast_new_binop(AST_SEQ, left, right);
      if(!combined)
        return NULL;
      left = combined;
    }
  }
  return left;
}

ast_t *vega_parse(const char *line)
{
  lexer_t L;
  lex_init(&L, line);

  ast_t *root = parse_list(&L);
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
