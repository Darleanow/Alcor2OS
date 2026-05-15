/**
 * @file user/shell/parse.c
 * @brief Recursive-descent parser building vega AST nodes from tokens.
 *
 * Grammar:
 *
 *   script   := list EOF
 *   list     := and_or (SEMI and_or)*      -- empty separators ignored
 *                                          -- newlines lex as SEMI too,
 *                                          -- so multi-line `{ ... }` works
 *   and_or   := pipeline ((AND | OR) pipeline)*
 *   pipeline := unit (PIPE unit)*
 *   unit     := if_stmt | while_stmt | for_stmt | fn_stmt | simple_command
 *   if_stmt  := 'if' and_or '{' list '}' ('else' (if_stmt | '{' list '}'))?
 *   while_stmt := 'while' and_or '{' list '}'
 *   for_stmt := 'for' WORD 'in' (WORD | STRING)* '{' list '}'
 *   fn_stmt  := 'fn' WORD '(' WORD* ')' '{' list '}'
 *   simple_command := (word_or_redir)+
 *   word_or_redir  := WORD | STRING
 *                   | (REDIR_OUT | REDIR_APPEND | REDIR_IN | HERESTRING) WORD
 *                   | HEREDOC WORD                  -- consumes lines after the
 *                                                   -- current one as the body,
 *                                                   -- terminated by a line of
 *                                                   -- the delimiter
 *
 * `if`/`else`/`while`/`for`/`in` are recognised as reserved words only when
 * they appear in the command position (first token of a unit / right after
 * the closing brace of an if branch); elsewhere they are ordinary
 * identifiers and can be passed as arguments.
 */

#include <stdlib.h>
#include <vega/frontend/lexer.h>
#include <vega/frontend/parse.h>
#include <vega/shell.h>

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
  case TOK_REDIR_OUT:
    *out = REDIR_OUT;
    return 1;
  case TOK_REDIR_APPEND:
    *out = REDIR_APPEND;
    return 1;
  case TOK_REDIR_IN:
    *out = REDIR_IN;
    return 1;
  case TOK_HERESTRING:
    *out = REDIR_HERESTRING;
    return 1;
  default:
    return 0;
  }
}

static ast_t *parse_command(lexer_t *L)
{
  ast_t *n = ast_new_cmd();
  if(!n)
    return NULL;

  while(1) {
    tok_t        t = lex_peek(L);
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

    if(t.kind == TOK_HEREDOC) {
      lex_next(L); /* consume `<<` */

      tok_t delim_tok = lex_next(L);
      if(delim_tok.kind != TOK_WORD && delim_tok.kind != TOK_STRING) {
        diag_expected_after(TOK_HEREDOC);
        lex_token_free(&delim_tok);
        L->error = 1;
        ast_free(n);
        return NULL;
      }

      char *body = lex_read_heredoc_body(L, delim_tok.text);
      free(delim_tok.text);
      if(!body) {
        ast_free(n);
        return NULL;
      }

      if(ast_cmd_add_redir(n, REDIR_HEREDOC, body) < 0) {
        free(body);
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

  /* `cmd!` postfix sugar: strip a trailing `!` from the command name and
   * mark the cmd as fail-fast. A bare `!` is left alone (would otherwise
   * become an empty argv[0]). */
  char *first = n->u.cmd.argv[0];
  int   flen  = 0;
  while(first[flen])
    flen++;
  if(flen > 1 && first[flen - 1] == '!') {
    first[flen - 1]    = '\0';
    n->u.cmd.fail_fast = 1;
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

static ast_t *parse_while(lexer_t *L)
{
  /* The opening `while` keyword has already been consumed by parse_unit. */
  ast_t *cond = parse_and_or(L);
  if(!cond || L->error) {
    diag_expected_after(TOK_WORD);
    ast_free(cond);
    L->error = 1;
    return NULL;
  }

  ast_t *body = parse_brace_body(L);
  if(L->error) {
    ast_free(cond);
    return NULL;
  }

  return ast_new_while(cond, body);
}

static ast_t *parse_for(lexer_t *L)
{
  /* The opening `for` keyword has already been consumed by parse_unit. */
  tok_t name_tok = lex_next(L);
  if(name_tok.kind != TOK_WORD) {
    diag_unexpected(name_tok.kind);
    lex_token_free(&name_tok);
    L->error = 1;
    return NULL;
  }

  if(!match_keyword(L, "in")) {
    sh_puts("vega: expected 'in' after for variable\n");
    free(name_tok.text);
    L->error = 1;
    return NULL;
  }

  ast_t *n = ast_new_for(name_tok.text);
  if(!n) {
    L->error = 1;
    return NULL;
  }

  while(1) {
    tok_t t = lex_peek(L);
    if(t.kind != TOK_WORD && t.kind != TOK_STRING)
      break;
    lex_next(L);
    if(ast_for_push_word(n, t.text) < 0) {
      free(t.text);
      ast_free(n);
      return NULL;
    }
  }

  ast_t *body = parse_brace_body(L);
  if(L->error) {
    ast_free(n);
    return NULL;
  }
  ast_for_set_body(n, body);
  return n;
}

static ast_t *parse_fn(lexer_t *L)
{
  /* The opening `fn` keyword has already been consumed by parse_unit. */
  tok_t name_tok = lex_next(L);
  if(name_tok.kind != TOK_WORD) {
    diag_unexpected(name_tok.kind);
    lex_token_free(&name_tok);
    L->error = 1;
    return NULL;
  }

  if(lex_peek(L).kind != TOK_LPAREN) {
    sh_puts("vega: expected '(' after fn name\n");
    free(name_tok.text);
    L->error = 1;
    return NULL;
  }
  lex_next(L); /* consume '(' */

  /* Args are space-separated WORDs; commas aren't word-delimiters in the
   * lexer so we'd parse them as part of names. Keep the syntax simple. */
  char **arg_names = NULL;
  int    n_args    = 0;
  int    cap       = 0;
  while(lex_peek(L).kind == TOK_WORD) {
    tok_t t = lex_next(L);
    if(n_args >= cap) {
      int    new_cap = (cap == 0) ? 4 : cap * 2;
      char **new_arr = (char **)realloc(arg_names, sizeof(char *) * new_cap);
      if(!new_arr) {
        free(t.text);
        for(int i = 0; i < n_args; i++)
          free(arg_names[i]);
        free(arg_names);
        free(name_tok.text);
        L->error = 1;
        return NULL;
      }
      arg_names = new_arr;
      cap       = new_cap;
    }
    arg_names[n_args++] = t.text;
  }

  if(lex_peek(L).kind != TOK_RPAREN) {
    diag_unexpected(lex_peek(L).kind);
    L->error = 1;
    for(int i = 0; i < n_args; i++)
      free(arg_names[i]);
    free(arg_names);
    free(name_tok.text);
    return NULL;
  }
  lex_next(L); /* consume ')' */

  ast_t *body = parse_brace_body(L);
  if(L->error) {
    for(int i = 0; i < n_args; i++)
      free(arg_names[i]);
    free(arg_names);
    free(name_tok.text);
    return NULL;
  }

  return ast_new_fn(name_tok.text, arg_names, n_args, body);
}

/* A "unit" is a single command in the pipeline grammar — either a compound
 * statement (`if`, `while`, `for`, `fn`) or a simple command. */
static ast_t *parse_unit(lexer_t *L)
{
  tok_t t = lex_peek(L);
  if(t.kind == TOK_WORD && sh_strcmp(t.text, "if") == 0) {
    lex_next(L);
    lex_token_free(&t);
    return parse_if(L);
  }
  if(t.kind == TOK_WORD && sh_strcmp(t.text, "while") == 0) {
    lex_next(L);
    lex_token_free(&t);
    return parse_while(L);
  }
  if(t.kind == TOK_WORD && sh_strcmp(t.text, "for") == 0) {
    lex_next(L);
    lex_token_free(&t);
    return parse_for(L);
  }
  if(t.kind == TOK_WORD && sh_strcmp(t.text, "fn") == 0) {
    lex_next(L);
    lex_token_free(&t);
    return parse_fn(L);
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
