/**
 * @file src/drivers/console/console.c
 * @brief Boot/panic logger on the linear framebuffer.
 *
 * Minimal text output for the kernel: an 8x16 ISO Latin-1 bitmap font,
 * cursor advance, scroll, plus @c \\n / @c \\r / @c \\b / @c \\t. No escape
 * sequences, no SGR, no charset switching, no UTF-8 multibyte. Anything
 * richer (ncurses, ANSI colors, line drawing, Fira) lives in the user-space
 * terminal at @c user/shell/platform/fb_tty.c.
 *
 * Reached from kernel boot prints (main.c, sched, mm, ...) and from
 * @c sys_write whenever a process writes to stdout/stderr without an OFT
 * entry — which after the shell takes over is essentially never.
 */

#include "font.h"
#include <alcor2/drivers/console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <stdarg.h>

#define FONT_W 8
#define FONT_H 16

static struct
{
  volatile u8 *base;
  u64          width;
  u64          height;
  u64          pitch_bytes;
  u8           bytes_pp;
  u32          fg;
  u32          bg;
  u32          cursor_x;
  u32          cursor_y;
} ctx;

/** @brief Write a 24bpp pixel as B,G,R in little-endian order. */
static void fb_store24(volatile u8 *p, u32 c)
{
  p[0] = (u8)(c & 0xffu);
  p[1] = (u8)((c >> 8) & 0xffu);
  p[2] = (u8)((c >> 16) & 0xffu);
}

/** @brief Write a 16bpp pixel by reducing @p c to RGB565. */
static void fb_store16(volatile u8 *p, u32 c)
{
  u32 r      = (c >> 16) & 0xffu;
  u32 g      = (c >> 8) & 0xffu;
  u32 b      = c & 0xffu;
  u16 rgb565 = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  p[0]       = (u8)(rgb565 & 0xffu);
  p[1]       = (u8)(rgb565 >> 8);
}

/** @brief Paint one pixel at (@p x, @p y) honoring the framebuffer's bpp. */
static void fb_put_pixel(u32 x, u32 y, u32 color)
{
  if(x >= ctx.width || y >= ctx.height)
    return;
  volatile u8 *p = ctx.base + (u64)y * ctx.pitch_bytes + (u64)x * ctx.bytes_pp;

  switch(ctx.bytes_pp) {
  case 4:
    *(volatile u32 *)p = color | 0xFF000000u;
    return;
  case 3:
    fb_store24(p, color);
    return;
  case 2:
    fb_store16(p, color);
    return;
  default:
    return;
  }
}

/** @brief Translate bits-per-pixel to bytes-per-pixel, clamped to [2, 4]. */
static u8 bytes_pp_from_bpp(u16 bpp)
{
  switch(bpp) {
  case 32:
    return 4;
  case 24:
    return 3;
  case 16:
    return 2;
  default: {
    u8 guess = (u8)(((unsigned)bpp + 7u) / 8u);
    if(guess >= 2u && guess <= 4u)
      return guess;
    return 4;
  }
  }
}

void console_init(void *fb, u64 width, u64 height, u64 pitch_bytes, u16 bpp)
{
  ctx.base        = (volatile u8 *)fb;
  ctx.width       = width;
  ctx.height      = height;
  ctx.pitch_bytes = pitch_bytes;
  ctx.bytes_pp    = bytes_pp_from_bpp(bpp);
  ctx.cursor_x    = 0;
  ctx.cursor_y    = 0;
  ctx.fg          = 0xFFFFFFFF;
  ctx.bg          = 0xFF000000;
}

void console_set_theme(console_theme_t theme)
{
  ctx.fg = theme.foreground | 0xFF000000u;
  ctx.bg = theme.background | 0xFF000000u;
}

/** @brief Fill rows [y0, y1) with @c ctx.bg, fast-path on 32 bpp. */
static void fb_clear_rectangle(u64 y0, u64 y1)
{
  if(ctx.bytes_pp == 4) {
    volatile u32 *row32  = (volatile u32 *)ctx.base;
    u64           pu     = ctx.pitch_bytes / 4ull;
    u64           pairs  = ctx.width / 2;
    u64           bgpair = ((u64)ctx.bg << 32) | ctx.bg;

    for(u64 y = y0; y < y1; y++) {
      volatile u64 *r = (volatile u64 *)(row32 + y * pu);
      for(u64 i = 0; i < pairs; i++)
        r[i] = bgpair;
      if(ctx.width & 1u)
        row32[y * pu + ctx.width - 1] = ctx.bg;
    }
    return;
  }
  for(u64 y = y0; y < y1; y++)
    for(u32 x = 0; x < (u32)ctx.width; x++)
      fb_put_pixel(x, (u32)y, ctx.bg);
}

void console_clear(void)
{
  fb_clear_rectangle(0, ctx.height);
  ctx.cursor_x = 0;
  ctx.cursor_y = 0;
}

/**
 * @brief Render glyph @p c from the Latin-1 atlas at (@p px, @p py).
 *
 * Unmapped bytes fall back to @c '?'.
 */
static void draw_glyph(char c, u32 px, u32 py)
{
  int gi = font_glyph_index((u8)c);
  if(gi < 0)
    gi = font_glyph_index((unsigned char)'?');
  if(gi < 0)
    return;

  const u8 *glyph = font_latin1[gi];
  for(int row = 0; row < FONT_H; row++) {
    u8 bits = glyph[row];
    for(int col = 0; col < FONT_W; col++) {
      u32 x = px + (u32)col;
      u32 y = py + (u32)row;
      fb_put_pixel(x, y, ((bits & (0x80u >> col)) != 0) ? ctx.fg : ctx.bg);
    }
  }
}

/** @brief Shift the framebuffer up by one cell row and clear the bottom row. */
static void scroll(void)
{
  u64 span = ctx.height - (u64)FONT_H;

  if(ctx.bytes_pp == 4) {
    volatile u32 *buf     = (volatile u32 *)ctx.base;
    u64           pu      = ctx.pitch_bytes / 4ull;
    u64           rowcopy = ctx.width * sizeof(u32);

    for(u64 y = 0; y < span; y++)
      kmemcpy(
          (void *)&buf[y * pu], (const void *)&buf[(y + (u64)FONT_H) * pu],
          rowcopy
      );
    fb_clear_rectangle(span, ctx.height);
    return;
  }

  u64 row_px = ctx.width * ctx.bytes_pp;
  for(u64 y = 0; y < span; y++) {
    volatile u8       *dst = ctx.base + y * ctx.pitch_bytes;
    const volatile u8 *src = ctx.base + (y + (u64)FONT_H) * ctx.pitch_bytes;
    kmemcpy((void *)dst, (const void *)src, row_px);
  }
  fb_clear_rectangle(span, ctx.height);
}

void console_putchar(char c)
{
  switch(c) {
  case '\n':
    ctx.cursor_x = 0;
    ctx.cursor_y += FONT_H;
    break;
  case '\r':
    ctx.cursor_x = 0;
    break;
  case '\t':
    ctx.cursor_x = (ctx.cursor_x + 32u) & ~31u;
    break;
  case '\b':
    if(ctx.cursor_x >= FONT_W) {
      ctx.cursor_x -= FONT_W;
      for(int row = 0; row < FONT_H; row++)
        for(int col = 0; col < FONT_W; col++)
          fb_put_pixel(
              ctx.cursor_x + (u32)col, ctx.cursor_y + (u32)row, ctx.bg
          );
    }
    break;
  default:
    draw_glyph(c, ctx.cursor_x, ctx.cursor_y);
    ctx.cursor_x += FONT_W;
    break;
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

void console_print(const char *s)
{
  while(*s)
    console_putchar(*s++);
}

/** @brief Print a signed decimal integer. */
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
  while(--i >= 0)
    console_putchar(buf[i]);
}

/** @brief Print an unsigned 64-bit decimal integer. */
static void print_uint(u64 n)
{
  char buf[32];
  int  i = 0;

  if(n == 0) {
    console_putchar('0');
    return;
  }
  while(n > 0) {
    buf[i++] = (char)('0' + (n % 10));
    n /= 10;
  }
  while(--i >= 0)
    console_putchar(buf[i]);
}

/** @brief Print an unsigned 64-bit value as @c 0x-prefixed 16-digit hex. */
static void print_hex(u64 n)
{
  static const char hex[] = "0123456789abcdef";
  console_print("0x");
  for(int s = 60; s >= 0; s -= 4)
    console_putchar(hex[(int)((n >> s) & 0xFULL)]);
}

void console_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  while(*fmt) {
    if(*fmt == '%' && *(fmt + 1)) {
      fmt++;
      if(*fmt == 'l')
        fmt++;

      switch(*fmt) {
      case 'd':
        print_int(va_arg(args, int));
        break;
      case 'u':
        print_uint((u64)va_arg(args, unsigned int));
        break;
      case 'x':
        print_hex(va_arg(args, u64));
        break;
      case 's':
        console_print(va_arg(args, const char *));
        break;
      case 'c':
        console_putchar((char)va_arg(args, int));
        break;
      case '%':
        console_putchar('%');
        break;
      default:
        console_putchar('%');
        console_putchar(*fmt);
        break;
      }
    } else
      console_putchar(*fmt);
    fmt++;
  }

  va_end(args);
}
