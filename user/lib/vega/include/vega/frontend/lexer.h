/**
 * @file user/shell/lexer.h
 * @brief vega tokenizer.
 *
 * Splits a line of input into tokens. The full vega token vocabulary is
 * recognised here so the lexer is stable across later phases — only the
 * parser/executor grow.
 */

#ifndef VEGA_LEXER_H
#define VEGA_LEXER_H

#include <stddef.h>

/**
 * Internal marker the lexer prepends to a token's text when the token came
 * from a single-quoted string. expand_word checks for this byte and emits
 * the remainder verbatim — no $-interpolation or {}-interpolation. Single
 * quotes are guaranteed not to contain this byte (raw input is text, not
 * binary), so the prefix is a safe in-band signal.
 */
#define VEGA_LITERAL_SENTINEL '\x01'

typedef enum
{
  TOK_EOF = 0,
  TOK_WORD,         /* bareword or single-quoted literal */
  TOK_STRING,       /* double-quoted; expansion lands later */
  TOK_PIPE,         /* |  */
  TOK_AND,          /* && */
  TOK_OR,           /* || */
  TOK_SEMI,         /* ;  */
  TOK_REDIR_OUT,    /* >  */
  TOK_REDIR_APPEND, /* >> */
  TOK_REDIR_IN,     /* <  */
  TOK_HEREDOC,      /* << */
  TOK_HERESTRING,   /* <<< */
  TOK_LBRACE,       /* {  */
  TOK_RBRACE,       /* }  */
  TOK_LPAREN,       /* (  */
  TOK_RPAREN,       /* )  */
} tok_kind_t;

/**
 * @brief One lexed token. @c text is heap-allocated for WORD/STRING and must
 * be freed by the consumer; for operator tokens it is NULL.
 */
typedef struct
{
  tok_kind_t kind;
  char      *text;
} tok_t;

typedef struct
{
  const char *cur;    /* read pointer into the source */
  tok_t       peeked; /* one-token lookahead */
  int         has_peek;
  int         error; /* set by the lexer on malformed input */
} lexer_t;

/** @brief Initialise a lexer over the given line. The line is not modified. */
void lex_init(lexer_t *L, const char *src);

/** @brief Consume and return the next token. */
tok_t lex_next(lexer_t *L);

/** @brief Peek at the next token without consuming. */
tok_t lex_peek(lexer_t *L);

/** @brief Free a token's heap storage if any. */
void lex_token_free(tok_t *t);

/** @brief Human-readable name for a token kind (for error messages). */
const char *tok_name(tok_kind_t k);

/**
 * @brief Consume a heredoc body from the lexer's stream. Skips the rest of the
 * current line (up to and including the next '\n'), then collects subsequent
 * lines as the body until a line whose content equals @p delim (with no
 * surrounding whitespace) is encountered. The lexer cursor is advanced past
 * the closing delimiter line.
 *
 * @return Heap-allocated body string (NUL-terminated). Empty body returns "".
 *         NULL on allocation failure or if EOF is reached before the
 *         delimiter (in which case L->error is set).
 */
char *lex_read_heredoc_body(lexer_t *L, const char *delim);

#endif /* VEGA_LEXER_H */
