/**
 * @file include/alcor2/console.h
 * @brief Framebuffer-based console output.
 *
 * Provides a simple text console using a linear framebuffer with a
 * built-in font. Supports scrolling, theming, and printf-style formatting.
 */

#ifndef ALCOR2_CONSOLE_H
#define ALCOR2_CONSOLE_H

#include <alcor2/types.h>

/**
 * @brief Console color theme.
 */
typedef struct
{
  u32 foreground; /**< Text color (RGB). */
  u32 background; /**< Background color (RGB). */
} console_theme_t;

/**
 * @brief Initialize the console with the given framebuffer.
 * @param fb Pointer to the linear framebuffer.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param pitch Bytes per scanline.
 */
void console_init(void *fb, u64 width, u64 height, u64 pitch);

/**
 * @brief Set the console color theme.
 * @param theme Color theme to use.
 */
void console_set_theme(console_theme_t theme);

/**
 * @brief Clear the console screen.
 */
void console_clear(void);

/**
 * @brief Write a single character to the console.
 * @param c Character to display.
 */
void console_putchar(char c);

/**
 * @brief Write a null-terminated string to the console.
 * @param s String to display.
 */
void console_print(const char *s);

/**
 * @brief Formatted console output (printf-style).
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void console_printf(const char *fmt, ...);

#endif
