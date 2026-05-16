/**
 * @file vega/internal/expand.h
 * @brief Sdk-internal expansion helpers. vega_setvar lives in <vega/vega.h>
 * because hosts implement `let` against it.
 */

#ifndef VEGA_EXPAND_H
#define VEGA_EXPAND_H

/** @brief Record the exit status of the most recently completed command. */
void expand_set_status(int status);

/**
 * @brief Look up a user variable.
 * @return Value string, or empty string if unset. Caller must not free.
 */
const char *expand_getvar(const char *name);

/**
 * @brief Expand $-syntax in @p src and return a new heap-allocated string.
 *        The caller takes ownership and must free.
 *
 * @return Newly allocated expanded string, or NULL on allocation failure.
 */
char *expand_word(const char *src);

#endif /* VEGA_EXPAND_H */
