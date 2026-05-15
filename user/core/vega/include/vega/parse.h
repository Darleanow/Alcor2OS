/**
 * @file vega/parse.h
 * @brief vega parser entry point.
 */

#ifndef VEGA_PARSE_H
#define VEGA_PARSE_H

#include <vega/ast.h>

/**
 * @brief Parse @p line into an AST.
 *
 * On success returns a freshly allocated AST that the caller must
 * @ref ast_free. On parse error prints a diagnostic and returns NULL.
 */
ast_t *vega_parse(const char *line);

#endif /* VEGA_PARSE_H */
