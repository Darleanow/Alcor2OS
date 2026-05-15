/**
 * @file user/shell/expand.h
 * @brief vega variable + command expansion.
 *
 * Currently handles $-prefixed expansion in argv strings:
 *   $VAR, ${VAR}   user variables (empty if unset)
 *   $?             last command exit status
 *   $$             current shell PID
 *
 * Phase 7b will add $(cmd) substitution; Phase 7c will add {var} brace
 * interpolation in double-quoted strings.
 */

#ifndef VEGA_EXPAND_H
#define VEGA_EXPAND_H

/** @brief Record the exit status of the most recently completed command. */
void expand_set_status(int status);

/**
 * @brief Set a user variable. Both name and value are copied.
 * @return 0 on success, -1 on allocation failure or if @p name is empty.
 */
int expand_setvar(const char *name, const char *value);

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
