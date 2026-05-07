/**
 * @file src/drivers/console/console.c
 * @brief Framebuffer console.
 *
 * Limine @a pitch is bytes per scanline. Pixel offset is
 * y * pitch_bytes + x * bytes_pp — do not assume 32 bpp.
 */

#include "font.h"
#include <alcor2/drivers/console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <stdarg.h>

#define FONT_W      8
#define FONT_H      16

#define ESC_BUF_MAX 64

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
  int          esc_state;
  char         esc_buf[ESC_BUF_MAX];
  int          esc_len;
  int cp437_mode; /**< 0: ISO Latin-1 atlas, 1: CP437 / VGA box glyphs. */
} ctx;

/** Match first three bytes of a little-endian u32 color (same as old u32 store
 * on 32 bpp). */
static void fb_store24(volatile u8 *p, u32 c)
{
  p[0] = (u8)(c & 0xffu);
  p[1] = (u8)((c >> 8) & 0xffu);
  p[2] = (u8)((c >> 16) & 0xffu);
}

static void fb_store16(volatile u8 *p, u32 c)
{
  u32 r      = (c >> 16) & 0xffu;
  u32 g      = (c >> 8) & 0xffu;
  u32 bo     = c & 0xffu;
  u16 rgb565 = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (bo >> 3));
  p[0]       = (u8)(rgb565 & 0xffu);
  p[1]       = (u8)(rgb565 >> 8);
}

static void fb_put_pixel(u32 x, u32 y, u32 color)
{
  if(x >= ctx.width || y >= ctx.height)
    return;
  volatile u8 *p = ctx.base + (u64)y * ctx.pitch_bytes + (u64)x * ctx.bytes_pp;

  switch(ctx.bytes_pp) {
  case 4:
    *(volatile u32 *)p = color;
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

/** Bytes per pixel from bpp; Limine QEMU is usually 32. */
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
  ctx.fg          = 0xFFFFFF;
  ctx.bg          = 0x000000;
  ctx.esc_state   = 0;
  ctx.esc_len     = 0;
  ctx.cp437_mode  = 0;
}

void console_set_theme(console_theme_t theme)
{
  ctx.fg = theme.foreground;
  ctx.bg = theme.background;
}

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

/** MSB is left pixel (classic PC font / font_bitmap.h). */
static void draw_glyph(char c, u32 px, u32 py)
{
  u8  uc = (u8)c;
  int gi = font_glyph_index(uc);
  if(gi < 0)
    gi = font_glyph_index((unsigned char)'?');
  if(gi < 0)
    return;

  const u8 *glyph = ctx.cp437_mode ? font_cp437[gi] : font_latin1[gi];

  for(int row = 0; row < FONT_H; row++) {
    u8 b = glyph[row];
    for(int col = 0; col < FONT_W; col++) {
      u32 x = px + (u32)col;
      u32 y = py + (u32)row;
      fb_put_pixel(x, y, ((b & (0x80u >> col)) != 0) ? ctx.fg : ctx.bg);
    }
  }
}

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

/** xterm-like 256-color palette (ANSI 38/48;5). */
static u32 ansi256_to_rgb(unsigned idx)
{
  static const u32 ansi16_col[16] = {
      0x000000ul, 0xAA0000ul, 0x00AA00ul, 0xAA5500ul, 0x0000AAul, 0xAA00AAul,
      0x00AAAAul, 0xAAAAAAul, 0x555555ul, 0xFF5555ul, 0x55FF55ul, 0xFFFF55ul,
      0x5555FFul, 0xFF55FFul, 0x55FFFFul, 0xFFFFFFul,
  };

  if(idx < 16u)
    return ansi16_col[idx];
  if(idx < 232u) {
    unsigned i     = idx - 16u;
    unsigned r6    = i / 36u;
    unsigned rem   = i % 36u;
    unsigned g6    = rem / 6u;
    unsigned b6    = rem % 6u;
    u32      rchan = (r6 == 0u) ? 0u : (55u + 40u * (r6 - 1u));
    u32      gchan = (g6 == 0u) ? 0u : (55u + 40u * (g6 - 1u));
    u32      bchan = (b6 == 0u) ? 0u : (55u + 40u * (b6 - 1u));
    return (rchan << 16) | (gchan << 8u) | bchan;
  }
  {
    unsigned k = idx - 232u;
    u32      v = 8u + 10u * k;
    return (v << 16) | (v << 8) | v;
  }
}

static void handle_sgr(void)
{
  if(ctx.esc_len < 1 || ctx.esc_buf[ctx.esc_len - 1] != 'm')
    return;

  int pn = ctx.esc_len - 1; /* excludes trailing ASCII 'm'. */
  if(pn <= 0) {
    ctx.fg         = 0xFFFFFFu;
    ctx.bg         = 0x000000u;
    ctx.cp437_mode = 0;
    return;
  }

  int pv[32];
  int np = 0;

  /* Parse ';'-separated unsigned numbers (missing numbers become 0). */
  int i = 0;
  while(i < pn && np < (int)(sizeof(pv) / sizeof(pv[0]))) {
    unsigned acc = 0u;
    int      dig = 0;
    while(i < pn && ctx.esc_buf[i] >= '0' && ctx.esc_buf[i] <= '9') {
      acc = acc * 10u + (unsigned)(ctx.esc_buf[i] - '0');
      i++;
      dig++;
    }
    pv[np++] = (int)((dig != 0) ? acc % 256u : 0u);
    if(i < pn && ctx.esc_buf[i] == ';')
      i++;
  }

  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];

    switch(p) {
    case 0:
      ctx.fg         = 0xFFFFFFu;
      ctx.bg         = 0x000000u;
      ctx.cp437_mode = 0;
      break;
    case 39:
      ctx.fg = 0xFFFFFFu;
      break;
    case 49:
      ctx.bg = 0x000000u;
      break;
    case 38:
      if(pi + 2 < np && pv[pi + 1] == 5) {
        ctx.fg = ansi256_to_rgb((unsigned)pv[pi + 2]);
        pi += 2;
      }
      break;
    case 48:
      if(pi + 2 < np && pv[pi + 1] == 5) {
        ctx.bg = ansi256_to_rgb((unsigned)pv[pi + 2]);
        pi += 2;
      }
      break;
    /* Private: atlas select (outside standard 30–37/90–97 foreground set). */
    case 50:
      ctx.cp437_mode = 0;
      break;
    case 51:
      ctx.cp437_mode = 1;
      break;
    default:
      break;
    }
  }
}

static void handle_ansi_sequence(void)
{
  if(ctx.esc_len < 1)
    return;

  char cmd = ctx.esc_buf[ctx.esc_len - 1];

  switch(cmd) {
  case 'J':
    if(ctx.esc_len >= 2 && ctx.esc_buf[0] == '2')
      console_clear();
    break;
  case 'H':
    ctx.cursor_x = 0;
    ctx.cursor_y = 0;
    break;
  case 'K':
    for(u32 x = ctx.cursor_x; x < ctx.width; x++) {
      for(int row = 0; row < FONT_H; row++)
        fb_put_pixel(x, ctx.cursor_y + (u32)row, ctx.bg);
    }
    break;
  case 'm':
    handle_sgr();
    break;
  default:
    break;
  }
}

void console_putchar(char c)
{
  if(ctx.esc_state == 1) {
    if(c == '[') {
      ctx.esc_state = 2;
      ctx.esc_len   = 0;
      return;
    }
    ctx.esc_state = 0;
  } else if(ctx.esc_state == 2) {
    if((c >= '0' && c <= '9') || c == ';') {
      if(ctx.esc_len < ESC_BUF_MAX - 1)
        ctx.esc_buf[ctx.esc_len++] = c;
      return;
    }
    ctx.esc_buf[ctx.esc_len++] = c;
    handle_ansi_sequence();
    ctx.esc_state = 0;
    return;
  }

  if(c == '\033') {
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
      switch(*fmt) {
      case 'd':
        print_int(va_arg(args, int)
        ); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 'u':
        print_uint((u64)va_arg(args, unsigned int)
        ); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 'x':
        print_hex(va_arg(args, u64)
        ); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 's':
        console_print(va_arg(args, const char *)
        ); // NOLINT(clang-analyzer-valist.Uninitialized)
        break;
      case 'c':
        console_putchar((char)va_arg(args, int)
        ); // NOLINT(clang-analyzer-valist.Uninitialized)
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
