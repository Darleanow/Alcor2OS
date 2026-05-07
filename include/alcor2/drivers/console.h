/**
 * @file include/alcor2/drivers/console.h
 * @brief Framebuffer-based console output.
 *
 * Framebuffer bpp is taken into account (@a bpp / bytes per pixel). Latin-1 and
 * CP437 atlases disagree on octets ≥0x80; select with @c ESC [ 50 m (ISO
 * Latin-1, default) vs @c ESC [ 51 m (CP437 / box drawing). Xterm 256-colour
 * SGR @c ESC [ 38 ; 5 ; n m and @c ESC [ 48 ; 5 ; n m set foreground/background
 * from the fixed palette.
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
