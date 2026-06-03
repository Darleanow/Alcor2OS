/* Unit tests for src/drivers/console/console.c — console_printf formatting.
 *
 * Like klog, outb is static inline asm. We block io.h and redirect
 * console_putchar output to a capture buffer via test-by-inclusion. */

#include "test_common.h"

#include <alcor2/types.h>

#include <stdarg.h>
#include <string.h>

#define CAPBUF_SIZE 512

static char     cap[CAPBUF_SIZE];
static unsigned cap_len;

/* Block io.h so the static inline asm outb/inb never get defined.
 * Instead provide function-like macros that capture debugcon writes and
 * silently drop everything else — no privileged instructions, no recursion. */
#define ALCOR2_IO_H

#define outb(port, val)                                                        \
  ((void)((unsigned)(port) == 0xE9u && cap_len < (CAPBUF_SIZE - 1u)            \
              ? (cap[cap_len++] = (char)(val), 0)                              \
              : 0))
#define inb(port)  ((u8)(0))
#define outw(p, v) ((void)(p), (void)(v))
#define inw(p)     ((u16)(0))
#define outl(p, v) ((void)(p), (void)(v))
#define inl(p)     ((u32)(0))
#define io_wait()  ((void)0)

static inline void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}

#include "../../src/drivers/console/console.c"

static int reset(void **state)
{
  (void)state;
  memset(cap, 0, sizeof(cap));
  cap_len = 0;
  /* ctx is file-static in console.c; we can access it here because of
   * test-by-inclusion. Set large dimensions so scroll() never triggers. */
  ctx.base     = NULL;
  ctx.width    = 1280;
  ctx.height   = 4096;
  ctx.cursor_x = 0;
  ctx.cursor_y = 0;
  return 0;
}

static const char *captured(void)
{
  cap[cap_len] = '\0';
  return cap;
}

/* console_printf tests */

static void prints_plain_string(void **state)
{
  (void)state;
  console_printf("hello");
  assert_string_equal(captured(), "hello");
}

static void percent_d_positive(void **state)
{
  (void)state;
  console_printf("%d", 42);
  assert_string_equal(captured(), "42");
}

static void percent_d_negative(void **state)
{
  (void)state;
  console_printf("%d", -7);
  assert_string_equal(captured(), "-7");
}

static void percent_d_zero(void **state)
{
  (void)state;
  console_printf("%d", 0);
  assert_string_equal(captured(), "0");
}

static void percent_u(void **state)
{
  (void)state;
  console_printf("%u", 255u);
  assert_string_equal(captured(), "255");
}

static void percent_u_zero(void **state)
{
  (void)state;
  console_printf("%u", 0u);
  assert_string_equal(captured(), "0");
}

static void percent_x_always_16_digits(void **state)
{
  (void)state;
  /* print_hex always emits 0x + 16 hex digits (full 64-bit). */
  console_printf("%x", (u64)0xFF);
  assert_string_equal(captured(), "0x00000000000000ff");
}

static void percent_x_zero(void **state)
{
  (void)state;
  console_printf("%x", (u64)0);
  assert_string_equal(captured(), "0x0000000000000000");
}

static void percent_s(void **state)
{
  (void)state;
  console_printf("%s", "world");
  assert_string_equal(captured(), "world");
}

static void percent_c(void **state)
{
  (void)state;
  console_printf("%c", (int)'Z');
  assert_string_equal(captured(), "Z");
}

static void percent_percent(void **state)
{
  (void)state;
  console_printf("100%%");
  assert_string_equal(captured(), "100%");
}

static void long_modifier_l_consumed(void **state)
{
  (void)state;
  console_printf("%ld", 99);
  assert_string_equal(captured(), "99");
}

static void unknown_specifier_echoes_verbatim(void **state)
{
  (void)state;
  console_printf("%q");
  assert_string_equal(captured(), "%q");
}

static void mixed_format(void **state)
{
  (void)state;
  console_printf("[%s] n=%d", "tag", 10);
  assert_string_equal(captured(), "[tag] n=10");
}

/* ---- framebuffer-backed tests ---- */

#define FB_COLS  16
#define FB_ROWS  4
#define FB_W     (FB_COLS * FONT_W)
#define FB_H     (FB_ROWS * FONT_H)

static u32 g_fb32[FB_H * FB_W];

static void setup_fb32(void)
{
  memset(g_fb32, 0, sizeof(g_fb32));
  console_init(g_fb32, FB_W, FB_H, FB_W * 4, 32);
}

/* console_init: sets dimensions */
static void console_init_sets_dimensions(void **state)
{
  (void)state;
  setup_fb32();
  assert_int_equal(ctx.width, FB_W);
  assert_int_equal(ctx.height, FB_H);
  assert_int_equal(ctx.bytes_pp, 4);
}

/* console_set_theme: updates fg/bg */
static void console_set_theme_updates_colors(void **state)
{
  (void)state;
  setup_fb32();
  console_theme_t t = {.foreground = 0xFF0000, .background = 0x000000};
  console_set_theme(t);
  assert_int_equal(ctx.fg, 0xFF0000 | 0xFF000000u);
  assert_int_equal(ctx.bg, 0xFF000000u);
}

/* console_clear: fills framebuffer with bg */
static void console_clear_fills_bg(void **state)
{
  (void)state;
  setup_fb32();
  /* Mark some pixels non-zero */
  g_fb32[0] = 0xDEADBEEF;
  console_clear();
  /* After clear, cursor at 0,0 and bg color used */
  assert_int_equal(ctx.cursor_x, 0);
  assert_int_equal(ctx.cursor_y, 0);
}

/* console_putchar: newline advances cursor_y */
static void putchar_newline_advances_y(void **state)
{
  (void)state;
  setup_fb32();
  console_putchar('\n');
  assert_int_equal(ctx.cursor_x, 0);
  assert_int_equal(ctx.cursor_y, FONT_H);
}

/* console_putchar: carriage return resets cursor_x */
static void putchar_cr_resets_x(void **state)
{
  (void)state;
  setup_fb32();
  ctx.cursor_x = 40;
  console_putchar('\r');
  assert_int_equal(ctx.cursor_x, 0);
}

/* console_putchar: tab aligns to 32-pixel boundary */
static void putchar_tab_aligns(void **state)
{
  (void)state;
  setup_fb32();
  ctx.cursor_x = 5;
  console_putchar('\t');
  assert_int_equal(ctx.cursor_x, 32); /* (5+32)&~31 = 32 */
}

/* console_putchar: backspace erases previous glyph */
static void putchar_backspace_erases(void **state)
{
  (void)state;
  setup_fb32();
  ctx.cursor_x = FONT_W;
  ctx.cursor_y = 0;
  console_putchar('\b');
  assert_int_equal(ctx.cursor_x, 0);
}

/* console_putchar: backspace at x=0 is noop */
static void putchar_backspace_at_zero_noop(void **state)
{
  (void)state;
  setup_fb32();
  ctx.cursor_x = 0;
  console_putchar('\b');
  assert_int_equal(ctx.cursor_x, 0);
}

/* console_putchar: printable char advances cursor_x */
static void putchar_printable_advances_x(void **state)
{
  (void)state;
  setup_fb32();
  console_putchar('A');
  assert_int_equal(ctx.cursor_x, FONT_W);
}

/* console_putchar: triggers scroll when cursor_y exceeds height */
static void putchar_scroll_on_overflow(void **state)
{
  (void)state;
  setup_fb32();
  /* Put cursor at last row */
  ctx.cursor_y = FB_H - FONT_H;
  console_putchar('\n');
  /* scroll() should fire; cursor_y stays within bounds */
  assert_true(ctx.cursor_y < (u32)FB_H);
}

/* console_putchar: line wrap at right edge */
static void putchar_wraps_at_right_edge(void **state)
{
  (void)state;
  setup_fb32();
  ctx.cursor_x = FB_W - FONT_W + 1; /* past the wrap threshold */
  console_putchar('X');
  /* After putting the char, cursor_x wraps and cursor_y advances */
  assert_int_equal(ctx.cursor_x, 0);
  assert_int_equal(ctx.cursor_y, FONT_H);
}

/* console_print: prints a string character by character */
static void console_print_prints_string(void **state)
{
  (void)state;
  setup_fb32();
  memset(cap, 0, sizeof(cap));
  cap_len = 0;
  console_print("hi\n");
  /* debugcon should have received h, i, \n */
  assert_int_equal(cap_len, 3);
  assert_int_equal(cap[0], 'h');
  assert_int_equal(cap[1], 'i');
  assert_int_equal(cap[2], '\n');
}

/* fb_put_pixel: OOB pixel is silently dropped */
static void fb_put_pixel_oob_no_crash(void **state)
{
  (void)state;
  setup_fb32();
  /* Writing past width/height must not crash */
  fb_put_pixel(FB_W + 10, 0, 0xFFFFFF);
  fb_put_pixel(0, FB_H + 10, 0xFFFFFF);
}

/* console_init: 24bpp framebuffer */
static void console_init_24bpp(void **state)
{
  (void)state;
  static u8 fb24[FB_H * FB_W * 3];
  console_init(fb24, FB_W, FB_H, FB_W * 3, 24);
  assert_int_equal(ctx.bytes_pp, 3);
}

/* console_init: 16bpp framebuffer */
static void console_init_16bpp(void **state)
{
  (void)state;
  static u8 fb16[FB_H * FB_W * 2];
  console_init(fb16, FB_W, FB_H, FB_W * 2, 16);
  assert_int_equal(ctx.bytes_pp, 2);
}

/* fb_clear_rectangle: non-32bpp path */
static void console_clear_non32bpp(void **state)
{
  (void)state;
  static u8 fb24[FB_H * FB_W * 3];
  memset(fb24, 0xFF, sizeof(fb24));
  console_init(fb24, FB_W, FB_H, FB_W * 3, 24);
  console_clear();
  assert_int_equal(ctx.cursor_x, 0);
}

/* scroll: non-32bpp path */
static void scroll_non32bpp(void **state)
{
  (void)state;
  static u8 fb24[FB_H * FB_W * 3];
  console_init(fb24, FB_W, FB_H, FB_W * 3, 24);
  ctx.cursor_y = FB_H - FONT_H;
  console_putchar('\n'); /* triggers scroll */
  assert_true(ctx.cursor_y < (u32)FB_H);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(prints_plain_string, reset),
      cmocka_unit_test_setup(percent_d_positive, reset),
      cmocka_unit_test_setup(percent_d_negative, reset),
      cmocka_unit_test_setup(percent_d_zero, reset),
      cmocka_unit_test_setup(percent_u, reset),
      cmocka_unit_test_setup(percent_u_zero, reset),
      cmocka_unit_test_setup(percent_x_always_16_digits, reset),
      cmocka_unit_test_setup(percent_x_zero, reset),
      cmocka_unit_test_setup(percent_s, reset),
      cmocka_unit_test_setup(percent_c, reset),
      cmocka_unit_test_setup(percent_percent, reset),
      cmocka_unit_test_setup(long_modifier_l_consumed, reset),
      cmocka_unit_test_setup(unknown_specifier_echoes_verbatim, reset),
      cmocka_unit_test_setup(mixed_format, reset),
      /* framebuffer-backed tests */
      cmocka_unit_test_setup(console_init_sets_dimensions, reset),
      cmocka_unit_test_setup(console_set_theme_updates_colors, reset),
      cmocka_unit_test_setup(console_clear_fills_bg, reset),
      cmocka_unit_test_setup(putchar_newline_advances_y, reset),
      cmocka_unit_test_setup(putchar_cr_resets_x, reset),
      cmocka_unit_test_setup(putchar_tab_aligns, reset),
      cmocka_unit_test_setup(putchar_backspace_erases, reset),
      cmocka_unit_test_setup(putchar_backspace_at_zero_noop, reset),
      cmocka_unit_test_setup(putchar_printable_advances_x, reset),
      cmocka_unit_test_setup(putchar_scroll_on_overflow, reset),
      cmocka_unit_test_setup(putchar_wraps_at_right_edge, reset),
      cmocka_unit_test_setup(console_print_prints_string, reset),
      cmocka_unit_test_setup(fb_put_pixel_oob_no_crash, reset),
      cmocka_unit_test_setup(console_init_24bpp, reset),
      cmocka_unit_test_setup(console_init_16bpp, reset),
      cmocka_unit_test_setup(console_clear_non32bpp, reset),
      cmocka_unit_test_setup(scroll_non32bpp, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
