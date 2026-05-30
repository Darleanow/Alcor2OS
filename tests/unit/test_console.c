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

/* kstdlib symbols used by console.c scroll/clear paths. */
static inline void *__attribute__((unused)) kmemset(void *d, int v, u64 n)
{
  return memset(d, v, n);
}
static inline void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
static inline void __attribute__((unused)) kzero(void *d, u64 n)
{
  memset(d, 0, n);
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
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
