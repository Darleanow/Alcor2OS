/**
 * @file include/alcor2/drivers/console.h
 * @brief Kernel boot/panic logger on the linear framebuffer.
 *
 * Plain 8x16 ISO Latin-1 bitmap text with cursor advance, scroll, and the
 * @c \\n / @c \\r / @c \\b / @c \\t controls. No escape sequences, no SGR,
 * no charset switching, no UTF-8 multibyte — strictly for kernel messages
 * before the user-space terminal takes over.
 *
 * The user-facing terminal emulator (Fira via FreeType + HarfBuzz, full
 * ANSI/CSI, DEC line drawing, 256-color SGR) lives in
 * @c user/shell/platform/fb_tty.c.
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
 * @param pitch_bytes Bytes per scanline (Limine @c pitch field).
 * @param bpp Bits per pixel (typically 24 or 32).
 */
void console_init(void *fb, u64 width, u64 height, u64 pitch_bytes, u16 bpp);

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
