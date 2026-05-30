/* Unit tests for src/drivers/console/klog.c — klogf() format parsing.
 *
 * outb() is static inline asm in io.h — we cannot stub it via the linker.
 * Instead we redefine it as a capturing macro before including klog.c
 * directly (test-by-inclusion), bypassing the io.h inline entirely. */

#include "test_common.h"

#include <alcor2/types.h>

#include <stdarg.h>
#include <string.h>

#define DEBUGCON_PORT 0xE9
#define CAPBUF_SIZE   256

static char     cap[CAPBUF_SIZE];
static unsigned cap_len;

/* Override outb before io.h gets to define it. */
#define ALCOR2_IO_H /* prevent io.h from being included by klog.c */
static inline void outb(u16 port, u8 val)
{
  if(port == DEBUGCON_PORT && cap_len < CAPBUF_SIZE - 1)
    cap[cap_len++] = (char)val;
}
static inline u8 inb(u16 p)
{
  (void)p;
  return 0;
}
static inline void outw(u16 p, u16 v)
{
  (void)p;
  (void)v;
}
static inline u16 inw(u16 p)
{
  (void)p;
  return 0;
}
static inline void outl(u16 p, u32 v)
{
  (void)p;
  (void)v;
}
static inline u32 inl(u16 p)
{
  (void)p;
  return 0;
}
static inline void io_wait(void) {}

/* Now pull in the entire klog.c translation unit. */
#include "../../src/drivers/console/klog.c"

static int reset(void **state)
{
  (void)state;
  memset(cap, 0, sizeof(cap));
  cap_len = 0;
  return 0;
}

/* Helpers */
static const char *captured(void)
{
  cap[cap_len] = '\0';
  return cap;
}

/* klogf tests */

static void plain_string(void **state)
{
  (void)state;
  klogf("hello");
  assert_string_equal(captured(), "hello");
}

static void percent_s(void **state)
{
  (void)state;
  klogf("%s", "world");
  assert_string_equal(captured(), "world");
}

static void percent_s_null(void **state)
{
  (void)state;
  klogf("%s", NULL);
  assert_string_equal(captured(), "(null)");
}

static void percent_d_positive(void **state)
{
  (void)state;
  klogf("%d", (i64)42);
  assert_string_equal(captured(), "42");
}

static void percent_d_negative(void **state)
{
  (void)state;
  klogf("%d", (i64)-7);
  assert_string_equal(captured(), "-7");
}

static void percent_d_zero(void **state)
{
  (void)state;
  klogf("%d", (i64)0);
  assert_string_equal(captured(), "0");
}

static void percent_u(void **state)
{
  (void)state;
  klogf("%u", (u64)255);
  assert_string_equal(captured(), "255");
}

static void percent_x(void **state)
{
  (void)state;
  klogf("%x", (u64)0xDEAD);
  assert_string_equal(captured(), "dead");
}

static void percent_x_zero(void **state)
{
  (void)state;
  klogf("%x", (u64)0);
  assert_string_equal(captured(), "0");
}

static void percent_c(void **state)
{
  (void)state;
  klogf("%c", (int)'A');
  assert_string_equal(captured(), "A");
}

static void percent_percent(void **state)
{
  (void)state;
  klogf("100%%");
  assert_string_equal(captured(), "100%");
}

static void long_modifier_skipped(void **state)
{
  (void)state;
  klogf("%ld", (i64)99);
  assert_string_equal(captured(), "99");
}

static void unknown_specifier_echoes_verbatim(void **state)
{
  (void)state;
  klogf("%q");
  assert_string_equal(captured(), "%q");
}

static void dangling_percent_stops_cleanly(void **state)
{
  (void)state;
  klogf("ok%");
  /* Must not crash; whatever output — just check it doesn't run off end. */
  assert_true(strncmp(captured(), "ok", 2) == 0);
}

static void mixed_format(void **state)
{
  (void)state;
  klogf("[%s] n=%d x=%x", "tag", (i64)10, (u64)0xFF);
  assert_string_equal(captured(), "[tag] n=10 x=ff");
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(plain_string, reset),
      cmocka_unit_test_setup(percent_s, reset),
      cmocka_unit_test_setup(percent_s_null, reset),
      cmocka_unit_test_setup(percent_d_positive, reset),
      cmocka_unit_test_setup(percent_d_negative, reset),
      cmocka_unit_test_setup(percent_d_zero, reset),
      cmocka_unit_test_setup(percent_u, reset),
      cmocka_unit_test_setup(percent_x, reset),
      cmocka_unit_test_setup(percent_x_zero, reset),
      cmocka_unit_test_setup(percent_c, reset),
      cmocka_unit_test_setup(percent_percent, reset),
      cmocka_unit_test_setup(long_modifier_skipped, reset),
      cmocka_unit_test_setup(unknown_specifier_echoes_verbatim, reset),
      cmocka_unit_test_setup(dangling_percent_stops_cleanly, reset),
      cmocka_unit_test_setup(mixed_format, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
