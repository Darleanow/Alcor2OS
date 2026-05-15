/**
 * @file core/vega/lexer.c
 * @brief vega tokenizer implementation.
 */

#include <vega/internal/lexer.h>
#include <stdlib.h>
#include <vega/host.h>

static int is_hspace(char c)
{
  return c == ' ' || c == '\t';
}

static int is_word_delim(char c)
{
  if(c == '\0' || is_hspace(c) || c == '\n')
    return 1;
  switch(c) {
  case '|':
  case '&':
  case ';':
  case '>':
  case '<':
  case '(':
  case ')':
  case '{':
  case '}':
  case '"':
  case '\'':
    return 1;
  default:
    return 0;
  }
}

/* Read a single-quoted token. Content is taken literally; no escapes.
 * Prefixes the heap text with VEGA_LITERAL_SENTINEL so the runtime's
 * expand_word knows to skip $/{}-interpolation for this argument. */
static tok_t read_squote(lexer_t *L)
{
  tok_t       t     = {TOK_WORD, NULL};
  const char *start = ++L->cur;
  while(*L->cur && *L->cur != '\'')
    L->cur++;
  if(*L->cur != '\'') {
    sh_puts("vega: unterminated single quote\n");
    L->error = 1;
    t.kind   = TOK_EOF;
    return t;
  }
  size_t len = (size_t)(L->cur - start);
  char  *out = (char *)malloc(len + 2);
  if(!out) {
    L->error = 1;
    t.kind   = TOK_EOF;
    return t;
  }
  out[0] = VEGA_LITERAL_SENTINEL;
  for(size_t i = 0; i < len; i++)
    out[i + 1] = start[i];
  out[len + 1] = '\0';
  t.text       = out;
  L->cur++; /* skip closing quote */
  return t;
}

/* Read a double-quoted token. Recognises \" \\ \$ as escapes. */
static tok_t read_dquote(lexer_t *L)
{
  tok_t       t     = {TOK_STRING, NULL};
  const char *start = ++L->cur;
  /* compute final length first to size the allocation */
  size_t      n    = 0;
  const char *scan = start;
  while(*scan && *scan != '"') {
    if(*scan == '\\' && scan[1] &&
       (scan[1] == '"' || scan[1] == '\\' || scan[1] == '$')) {
      scan++;
    }
    scan++;
    n++;
  }
  if(*scan != '"') {
    sh_puts("vega: unterminated double quote\n");
    L->error = 1;
    t.kind   = TOK_EOF;
    return t;
  }
  char *out = (char *)malloc(n + 1);
  if(!out) {
    L->error = 1;
    t.kind   = TOK_EOF;
    return t;
  }
  size_t      i = 0;
  const char *p = start;
  while(*p && *p != '"') {
    if(*p == '\\' && p[1] && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
      p++;
    }
    out[i++] = *p++;
  }
  out[i] = '\0';
  t.text = out;
  L->cur = p + 1; /* skip closing quote */
  return t;
}

/* Read an unquoted bareword. Handles \<char> escapes and ${...} (which would
 * otherwise be split by the lexer because '{' is a structural token). */
static tok_t read_word(lexer_t *L)
{
  tok_t       t     = {TOK_WORD, NULL};
  const char *start = L->cur;
  /* sizing pass */
  size_t      n    = 0;
  const char *scan = start;
  while(!is_word_delim(*scan)) {
    if(*scan == '\\' && scan[1]) {
      scan++;
      scan++;
      n++;
      continue;
    }
    if(*scan == '$' && scan[1] == '{') {
      while(*scan && *scan != '}') {
        scan++;
        n++;
      }
      if(*scan == '}') {
        scan++;
        n++;
      }
      continue;
    }
    if(*scan == '$' && scan[1] == '(') {
      scan++;
      n++; /* $ */
      scan++;
      n++; /* ( */
      int depth = 1;
      while(*scan && depth > 0) {
        if(*scan == '(')
          depth++;
        else if(*scan == ')') {
          if(--depth == 0)
            break;
        }
        scan++;
        n++;
      }
      if(*scan == ')') {
        scan++;
        n++;
      }
      continue;
    }
    scan++;
    n++;
  }
  char *out = (char *)malloc(n + 1);
  if(!out) {
    L->error = 1;
    t.kind   = TOK_EOF;
    return t;
  }
  size_t      i = 0;
  const char *p = start;
  while(!is_word_delim(*p)) {
    if(*p == '\\' && p[1]) {
      p++;
      out[i++] = *p++;
      continue;
    }
    if(*p == '$' && p[1] == '{') {
      while(*p && *p != '}')
        out[i++] = *p++;
      if(*p == '}')
        out[i++] = *p++;
      continue;
    }
    if(*p == '$' && p[1] == '(') {
      out[i++]  = *p++; /* $ */
      out[i++]  = *p++; /* ( */
      int depth = 1;
      while(*p && depth > 0) {
        if(*p == '(')
          depth++;
        else if(*p == ')') {
          if(--depth == 0)
            break;
        }
        out[i++] = *p++;
      }
      if(*p == ')')
        out[i++] = *p++;
      continue;
    }
    out[i++] = *p++;
  }
  out[i] = '\0';
  L->cur = p;
  t.text = out;
  return t;
}

void lex_init(lexer_t *L, const char *src)
{
  L->cur      = src;
  L->has_peek = 0;
  L->error    = 0;
}

static tok_t scan_one(lexer_t *L)
{
  /* skip horizontal whitespace */
  while(is_hspace(*L->cur))
    L->cur++;

  tok_t t = {TOK_EOF, NULL};
  char  c = *L->cur;

  if(c == '\0')
    return t;
  if(c == '\n') {
    /* Newlines act as statement separators (equivalent to ';'). The
     * multi-line REPL stitches input lines with '\n' into one buffer; this
     * lets a brace body like `if pwd {\n echo hi\n}` parse as
     * `if pwd { echo hi ; }`. */
    L->cur++;
    t.kind = TOK_SEMI;
    return t;
  }

  switch(c) {
  case '|':
    L->cur++;
    if(*L->cur == '|') {
      L->cur++;
      t.kind = TOK_OR;
    } else {
      t.kind = TOK_PIPE;
    }
    return t;
  case '&':
    L->cur++;
    if(*L->cur == '&') {
      L->cur++;
      t.kind = TOK_AND;
    } else {
      sh_puts("vega: '&' (background) not yet supported\n");
      L->error = 1;
    }
    return t;
  case ';':
    L->cur++;
    t.kind = TOK_SEMI;
    return t;
  case '>':
    L->cur++;
    if(*L->cur == '>') {
      L->cur++;
      t.kind = TOK_REDIR_APPEND;
    } else {
      t.kind = TOK_REDIR_OUT;
    }
    return t;
  case '<':
    L->cur++;
    if(*L->cur == '<') {
      L->cur++;
      if(*L->cur == '<') {
        L->cur++;
        t.kind = TOK_HERESTRING;
      } else {
        t.kind = TOK_HEREDOC;
      }
    } else {
      t.kind = TOK_REDIR_IN;
    }
    return t;
  case '(':
    L->cur++;
    t.kind = TOK_LPAREN;
    return t;
  case ')':
    L->cur++;
    t.kind = TOK_RPAREN;
    return t;
  case '{':
    L->cur++;
    t.kind = TOK_LBRACE;
    return t;
  case '}':
    L->cur++;
    t.kind = TOK_RBRACE;
    return t;
  case '\'':
    return read_squote(L);
  case '"':
    return read_dquote(L);
  default:
    return read_word(L);
  }
}

tok_t lex_next(lexer_t *L)
{
  if(L->has_peek) {
    L->has_peek = 0;
    return L->peeked;
  }
  return scan_one(L);
}

tok_t lex_peek(lexer_t *L)
{
  if(!L->has_peek) {
    L->peeked   = scan_one(L);
    L->has_peek = 1;
  }
  return L->peeked;
}

void lex_token_free(tok_t *t)
{
  if(t->text) {
    free(t->text);
    t->text = NULL;
  }
}

/* Compare a buffer slice (start..end exclusive) to a NUL-terminated string. */
static int slice_eq(const char *start, const char *end, const char *s)
{
  while(start < end && *s) {
    if(*start != *s)
      return 0;
    start++;
    s++;
  }
  return start == end && *s == '\0';
}

char *lex_read_heredoc_body(lexer_t *L, const char *delim)
{
  /* Skip the rest of the current line. */
  while(*L->cur && *L->cur != '\n')
    L->cur++;
  if(*L->cur == '\n')
    L->cur++;

  /* Collect lines until one equals @p delim. */
  const char *body_start = L->cur;
  const char *body_end;
  while(*L->cur) {
    const char *line_start = L->cur;
    while(*L->cur && *L->cur != '\n')
      L->cur++;
    const char *line_end = L->cur;
    if(slice_eq(line_start, line_end, delim)) {
      if(*L->cur == '\n')
        L->cur++;
      body_end = line_start;
      goto done;
    }
    if(*L->cur == '\n')
      L->cur++;
    /* body_end is only read on the goto-done path above; no update needed */
  }
  /* Reached EOF without seeing the delimiter. */
  sh_puts("vega: unterminated heredoc (missing '");
  sh_puts(delim);
  sh_puts("')\n");
  L->error = 1;
  return NULL;

done: {
  size_t len = (size_t)(body_end - body_start);
  char  *out = (char *)malloc(len + 1);
  if(!out) {
    L->error = 1;
    return NULL;
  }
  for(size_t i = 0; i < len; i++)
    out[i] = body_start[i];
  out[len] = '\0';
  return out;
}
}

const char *tok_name(tok_kind_t k)
{
  switch(k) {
  case TOK_EOF:
    return "end-of-input";
  case TOK_WORD:
    return "word";
  case TOK_STRING:
    return "string";
  case TOK_PIPE:
    return "'|'";
  case TOK_AND:
    return "'&&'";
  case TOK_OR:
    return "'||'";
  case TOK_SEMI:
    return "';'";
  case TOK_REDIR_OUT:
    return "'>'";
  case TOK_REDIR_APPEND:
    return "'>>'";
  case TOK_REDIR_IN:
    return "'<'";
  case TOK_HEREDOC:
    return "'<<'";
  case TOK_HERESTRING:
    return "'<<<'";
  case TOK_LBRACE:
    return "'{'";
  case TOK_RBRACE:
    return "'}'";
  case TOK_LPAREN:
    return "'('";
  case TOK_RPAREN:
    return "')'";
  }
  return "?";
}
