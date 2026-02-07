/**
 * @file src/drivers/console/console.c
 * @brief Framebuffer console with ANSI color support.
 */

#include <stdarg.h>
#include "font.h"
#include <alcor2/console.h>

#define FONT_W 8
#define FONT_H 16

/** @brief Console state and framebuffer context. */
static struct
{
  volatile u32 *buffer;      /**< Framebuffer pointer */
  u64           width;       /**< Width in pixels */
  u64           height;      /**< Height in pixels */
  u64           pitch;       /**< Pitch in pixels */
  u32           cursor_x;    /**< Cursor X (chars) */
  u32           cursor_y;    /**< Cursor Y (chars) */
  u32           fg;          /**< Foreground color */
  u32           bg;          /**< Background color */
  int  esc_state;            /**< ANSI parser: 0=normal, 1=ESC, 2=[ */
  char esc_buf[16];          /**< ANSI escape buffer */
  int  esc_len;              /**< ANSI buffer length */
} ctx;

/**
 * @brief Initialize the framebuffer console
 * @param fb Framebuffer address
 * @param width Width in pixels
 * @param height Height in pixels
 * @param pitch Pitch in bytes (bytes per row)
 */
void console_init(void *fb, u64 width, u64 height, u64 pitch)
{
  ctx.buffer   = (volatile u32 *)fb;
  ctx.width    = width;
  ctx.height   = height;
  ctx.pitch    = pitch / sizeof(u32);
  ctx.cursor_x = 0;
  ctx.cursor_y = 0;
  ctx.fg       = 0xFFFFFF;
  ctx.bg       = 0x000000;
}

/**
 * @brief Set console color theme
 * @param theme Theme with foreground and background colors (RGB)
 */
// cppcheck-suppress unusedFunction
void console_set_theme(console_theme_t theme)
{
  ctx.fg = theme.foreground;
  ctx.bg = theme.background;
}

/**
 * @brief Clear the entire screen and reset cursor
 */
void console_clear(void)
{
  for(u64 y = 0; y < ctx.height; y++)
    for(u64 x = 0; x < ctx.width; x++)
      ctx.buffer[y * ctx.pitch + x] = ctx.bg;
  ctx.cursor_x = 0;
  ctx.cursor_y = 0;
}

/**
 * @brief Draw 8x16 glyph at pixel position.
 * @param c Character to draw.
 * @param px Pixel X.
 * @param py Pixel Y.
 */
static void draw_glyph(char c, u32 px, u32 py)
{
  if(c < 32 || c > 126)
    return;
  const u8 *glyph = font_data[c - 32];

  for(int row = 0; row < FONT_H; row++) {
    for(int col = 0; col < FONT_W; col++) {
      u32 x = px + col;
      u32 y = py + row;
      if(x < ctx.width && y < ctx.height) {
        u32 color = (glyph[row] & (0x80 >> col)) ? ctx.fg : ctx.bg;
        ctx.buffer[y * ctx.pitch + x] = color;
      }
    }
  }
}

/**
 * @brief Scroll framebuffer up by one line.
 */
static void scroll(void)
{
  for(u64 y = 0; y < ctx.height - FONT_H; y++)
    for(u64 x = 0; x < ctx.width; x++)
      ctx.buffer[y * ctx.pitch + x] = ctx.buffer[(y + FONT_H) * ctx.pitch + x];

  for(u64 y = ctx.height - FONT_H; y < ctx.height; y++)
    for(u64 x = 0; x < ctx.width; x++)
      ctx.buffer[y * ctx.pitch + x] = ctx.bg;
}

/**
 * @brief Process ANSI escape sequence.
 */
static void handle_ansi_sequence(void)
{
  /* Parse the escape sequence in esc_buf */
  /* Common sequences:
   * [2J  - clear screen
   * [H   - cursor home
   * [nA  - cursor up n
   * [nB  - cursor down n
   * [nC  - cursor forward n
   * [nD  - cursor back n
   */
  char cmd = ctx.esc_buf[ctx.esc_len - 1];

  switch(cmd) {
  case 'J': /* Erase display */
    if(ctx.esc_len >= 2 && ctx.esc_buf[0] == '2') {
      console_clear();
    }
    break;
  case 'H': /* Cursor home */
    ctx.cursor_x = 0;
    ctx.cursor_y = 0;
    break;
  case 'K': /* Erase line */
    /* Clear from cursor to end of line */
    for(u32 x = ctx.cursor_x; x < ctx.width; x++) {
      for(int row = 0; row < FONT_H; row++) {
        ctx.buffer[(ctx.cursor_y + row) * ctx.pitch + x] = ctx.bg;
      }
    }
    break;
  default: break;
  }
}

/**
 * @brief Write a single character to the console
 * @param c Character to write (supports \n, \r, \t, \b, and ANSI escapes)
 */
void console_putchar(char c)
{
  /* ANSI escape sequence parser */
  if(ctx.esc_state == 1) {
    if(c == '[') {
      ctx.esc_state = 2;
      ctx.esc_len   = 0;
      return;
    }
    ctx.esc_state = 0;
    /* Fall through to print the character */
  } else if(ctx.esc_state == 2) {
    if((c >= '0' && c <= '9') || c == ';') {
      if(ctx.esc_len < 15) {
        ctx.esc_buf[ctx.esc_len++] = c;
      }
      return;
    }
    /* End of sequence */
    ctx.esc_buf[ctx.esc_len++] = c;
    handle_ansi_sequence();
    ctx.esc_state = 0;
    return;
  }

  if(c == '\033') { /* ESC */
    ctx.esc_state = 1;
    return;
  }

  switch(c) {
  case '\n':
    ctx.cursor_x = 0;
    ctx.cursor_y += FONT_H;
    break;
  case '\r':
    ctx.cursor_x = 0;
    break;
  case '\t':
    ctx.cursor_x = (ctx.cursor_x + 32) & ~31;
    break;
  case '\b':
    /* Backspace: move cursor back and erase character */
    if(ctx.cursor_x >= FONT_W) {
      ctx.cursor_x -= FONT_W;
      /* Erase the character by drawing background */
      for(int row = 0; row < FONT_H; row++) {
        for(int col = 0; col < FONT_W; col++) {
          u32 x = ctx.cursor_x + col;
          u32 y = ctx.cursor_y + row;
          if(x < ctx.width && y < ctx.height) {
            ctx.buffer[y * ctx.pitch + x] = ctx.bg;
          }
        }
      }
    }
    break;
  default:
    draw_glyph(c, ctx.cursor_x, ctx.cursor_y);
    ctx.cursor_x += FONT_W;
  }

  if(ctx.cursor_x + FONT_W > ctx.width) {
    ctx.cursor_x = 0;
    ctx.cursor_y += FONT_H;
  }

  if(ctx.cursor_y + FONT_H > ctx.height) {
    scroll();
    ctx.cursor_y -= FONT_H;
  }
}

/**
 * @brief Write a null-terminated string to the console.
 * @param s String to display.
 */
void console_print(const char *s)
{
  while(*s)
    console_putchar(*s++);
}

/**
 * @brief Print a signed integer to the console.
 * @param n Integer to print.
 */
static void print_int(int n)
{
  char buf[32];
  int  i = 0;

  if(n == 0) {
    console_putchar('0');
    return;
  }

  if(n < 0) {
    console_putchar('-');
    n = -n;
  }

  while(n > 0) {
    buf[i++] = (char)('0' + (n % 10));
    n /= 10;
  }

  while(--i >= 0) {
    console_putchar(buf[i]);
  }
}

/**
 * @brief Print an unsigned 64-bit integer in hexadecimal format.
 * @param n Integer to print (prefixed with "0x").
 */
static void print_hex(u64 n)
{
  const char hex[] = "0123456789abcdef";
  console_print("0x");
  for(int i = 60; i >= 0; i -= 4)
    console_putchar(hex[(n >> i) & 0xF]);
}

/**
 * @brief Formatted console output (printf-style).
 * 
 * Supports the following format specifiers:
 * - %d: signed integer
 * - %x: unsigned 64-bit hexadecimal (with 0x prefix)
 * - %s: null-terminated string
 * - %c: single character
 * - %%: literal percent sign
 * 
 * @param fmt Format string.
 * @param ... Variable arguments matching format specifiers.
 */
void console_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  while(*fmt) {
    if(*fmt == '%' && *(fmt + 1)) {
      fmt++;
      switch(*fmt) {
      case 'd':
        print_int(va_arg(args, int)); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 'x':
        print_hex(va_arg(args, u64)); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 's':
        console_print(va_arg(args, const char *)); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 'c':
        console_putchar((char)va_arg(args, int)); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case '%':
        console_putchar('%');
        break;
      default:
        console_putchar('%');
        console_putchar(*fmt);
        break;
      }
    } else {
      console_putchar(*fmt);
    }
    fmt++;
  }

  va_end(args);
}
