/**
 * @file user/shell/parse.c
 * @brief Recursive-descent parser building vega AST nodes from tokens.
 *
 * Grammar:
 *
 *   script   := list EOF
 *   list     := and_or (SEMI and_or)*      -- empty separators ignored
 *   and_or   := pipeline ((AND | OR) pipeline)*
 *   pipeline := unit (PIPE unit)*
 *   unit     := if_stmt | simple_command
 *   if_stmt  := 'if' and_or '{' list '}' ('else' (if_stmt | '{' list '}'))?
 *   simple_command := (word_or_redir)+
 *   word_or_redir  := WORD | STRING
 *                   | (REDIR_OUT | REDIR_APPEND | REDIR_IN | HERESTRING) WORD
 *
 * `if`/`else` are recognised as reserved words only when they appear in the
 * command position (first token of a unit / right after the closing brace of
 * an if branch); elsewhere they are ordinary identifiers and can be passed as
 * arguments. while/for/heredocs land in later phases.
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

static int redir_kind_from_token(tok_kind_t k, redir_kind_t *out)
{
  switch(k) {
    case TOK_REDIR_OUT:    *out = REDIR_OUT;        return 1;
    case TOK_REDIR_APPEND: *out = REDIR_APPEND;     return 1;
    case TOK_REDIR_IN:     *out = REDIR_IN;         return 1;
    case TOK_HERESTRING:   *out = REDIR_HERESTRING; return 1;
    default:               return 0;
  }
}

static ast_t *parse_command(lexer_t *L)
{
  ast_t *n = ast_new_cmd();
  if(!n)
    return NULL;

  while(1) {
    tok_t t = lex_peek(L);
    redir_kind_t rk;

    if(t.kind == TOK_WORD || t.kind == TOK_STRING) {
      lex_next(L);
      if(ast_cmd_push_arg(n, t.text) < 0) {
        free(t.text);
        ast_free(n);
        return NULL;
      }
      continue;
    }

    if(redir_kind_from_token(t.kind, &rk)) {
      tok_kind_t op_kind = t.kind;
      lex_next(L); /* consume the redir operator */

      tok_t target = lex_next(L);
      if(target.kind != TOK_WORD && target.kind != TOK_STRING) {
        diag_expected_after(op_kind);
        lex_token_free(&target);
        L->error = 1;
        ast_free(n);
        return NULL;
      }
      if(ast_cmd_add_redir(n, rk, target.text) < 0) {
        free(target.text);
        ast_free(n);
        return NULL;
      }
      continue;
    }

    break;
  }

  if(n->u.cmd.argc == 0) {
    ast_free(n);
    return NULL;
  }
  return n;
}

static ast_t *parse_list(lexer_t *L);
static ast_t *parse_and_or(lexer_t *L);
static ast_t *parse_unit(lexer_t *L);

/* Returns 1 and consumes the token if the next token is a word matching
 * @p keyword. Otherwise leaves the lexer untouched and returns 0. */
static int match_keyword(lexer_t *L, const char *keyword)
{
  tok_t t = lex_peek(L);
  if(t.kind != TOK_WORD || sh_strcmp(t.text, keyword) != 0)
    return 0;
  lex_next(L);
  lex_token_free(&t);
  return 1;
}

/* Parses `{ list }`. Caller has already verified the leading `{`. Returns
 * the body's AST (possibly NULL for empty `{ }`); on error returns NULL with
 * L->error set. */
static ast_t *parse_brace_body(lexer_t *L)
{
  if(lex_peek(L).kind != TOK_LBRACE) {
    diag_unexpected(lex_peek(L).kind);
    L->error = 1;
    return NULL;
  }
  lex_next(L); /* consume '{' */

  ast_t *body = parse_list(L);
  if(L->error) {
    ast_free(body);
    return NULL;
  }

  if(lex_peek(L).kind != TOK_RBRACE) {
    diag_unexpected(lex_peek(L).kind);
    L->error = 1;
    ast_free(body);
    return NULL;
  }
  lex_next(L); /* consume '}' */
  return body;
}

static ast_t *parse_if(lexer_t *L)
{
  /* The opening `if` keyword has already been consumed by parse_unit. */
  ast_t *cond = parse_and_or(L);
  if(!cond || L->error) {
    diag_expected_after(TOK_WORD);
    ast_free(cond);
    L->error = 1;
    return NULL;
  }

  ast_t *then_branch = parse_brace_body(L);
  if(L->error) {
    ast_free(cond);
    return NULL;
  }

  ast_t *else_branch = NULL;
  if(match_keyword(L, "else")) {
    if(match_keyword(L, "if")) {
      else_branch = parse_if(L);
    } else {
      else_branch = parse_brace_body(L);
    }
    if(L->error) {
      ast_free(cond);
      ast_free(then_branch);
      ast_free(else_branch);
      return NULL;
    }
  }

  return ast_new_if(cond, then_branch, else_branch);
}

/* A "unit" is a single command in the pipeline grammar — either a compound
 * statement (currently just `if`) or a simple command. */
static ast_t *parse_unit(lexer_t *L)
{
  tok_t t = lex_peek(L);
  if(t.kind == TOK_WORD && sh_strcmp(t.text, "if") == 0) {
    lex_next(L);
    lex_token_free(&t);
    return parse_if(L);
  }
  return parse_command(L);
}

static ast_t *parse_pipeline(lexer_t *L)
{
  ast_t *first = parse_unit(L);
  if(!first)
    return NULL;

  if(lex_peek(L).kind != TOK_PIPE)
    return first; /* single unit, no pipeline wrapper */

  ast_t *pipe_node = ast_new_pipeline();
  if(!pipe_node) {
    ast_free(first);
    return NULL;
  }
  if(ast_pipeline_push(pipe_node, first) < 0) {
    ast_free(first);
    ast_free(pipe_node);
    return NULL;
  }

  while(lex_peek(L).kind == TOK_PIPE) {
    lex_next(L); /* consume '|' */
    ast_t *next = parse_unit(L);
    if(!next) {
      diag_expected_after(TOK_PIPE);
      ast_free(pipe_node);
      return NULL;
    }
    if(ast_pipeline_push(pipe_node, next) < 0) {
      ast_free(next);
      ast_free(pipe_node);
      return NULL;
    }
  }
  return pipe_node;
}

static ast_t *parse_and_or(lexer_t *L)
{
  ast_t *left = parse_pipeline(L);
  if(!left)
    return NULL;

  while(1) {
    tok_t t = lex_peek(L);
    if(t.kind != TOK_AND && t.kind != TOK_OR)
      break;
    lex_next(L);

    ast_t *right = parse_pipeline(L);
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
