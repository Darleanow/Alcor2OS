#include "test_common.h"

#include <alcor2/types.h>

#include <string.h>

#define ALCOR2_IO_H
#define outb(p, v) ((void)(p), (void)(v))
#define inb(p)     ((u8)0)
#define outw(p, v) ((void)(p), (void)(v))
#define inw(p)     ((u16)0)
#define outl(p, v) ((void)(p), (void)(v))
#define inl(p)     ((u32)0)
#define io_wait()  ((void)0)

#include <kernel/drivers/fb_console/internal.h>
fb_console_ctx_t fb_ctx;
#include "../../src/kernel/drivers/fb_console/palette.c"

static void ansi16_fg_index_0_is_black(void **state)
{
  (void)state;
  assert_int_equal(ansi16_fg[0], 0x45475au);
}

static void ansi16_fg_bright_differs_from_normal(void **state)
{
  (void)state;
  assert_int_not_equal(ansi16_fg_bright[0], ansi16_fg[0]);
}

static void ansi256_indices_0_to_7_map_to_fg(void **state)
{
  (void)state;
  for(unsigned i = 0; i < 8; i++)
    assert_int_equal(ansi256_to_rgb(i), ansi16_fg[i]);
}

static void ansi256_indices_8_to_15_map_to_bright(void **state)
{
  (void)state;
  for(unsigned i = 0; i < 8; i++)
    assert_int_equal(ansi256_to_rgb(8 + i), ansi16_fg_bright[i]);
}

static void ansi256_cube_index_16_is_black(void **state)
{
  (void)state;
  assert_int_equal(ansi256_to_rgb(16), 0x000000u);
}

static void ansi256_cube_index_231_is_max(void **state)
{
  (void)state;
  /* r=g=b=5 → axis(5)=215=0xD7 */
  assert_int_equal(ansi256_to_rgb(231), 0xD7D7D7u);
}

static void ansi256_cube_pure_red(void **state)
{
  (void)state;
  /* index 196: r=5,g=0,b=0 → 0xD70000 */
  assert_int_equal(ansi256_to_rgb(196), 0xD70000u);
}

static void ansi256_cube_pure_green(void **state)
{
  (void)state;
  /* index 46: r=0,g=5,b=0 → 0x00D700 */
  assert_int_equal(ansi256_to_rgb(46), 0x00D700u);
}

static void ansi256_cube_pure_blue(void **state)
{
  (void)state;
  /* index 21: r=0,g=0,b=5 → 0x0000D7 */
  assert_int_equal(ansi256_to_rgb(21), 0x0000D7u);
}

static void ansi256_grey_base(void **state)
{
  (void)state;
  /* index 232: v = 8, rgb = 0x080808 */
  assert_int_equal(ansi256_to_rgb(232), 0x080808u);
}

static void ansi256_grey_top(void **state)
{
  (void)state;
  /* index 255: v = 8 + 10*23 = 238 */
  assert_int_equal(ansi256_to_rgb(255), 0xEEEEEEu);
}

static void ansi256_grey_step_is_10(void **state)
{
  (void)state;
  u32 v232 = ansi256_to_rgb(232) & 0xFF;
  u32 v233 = ansi256_to_rgb(233) & 0xFF;
  assert_int_equal(v233 - v232, 10);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(ansi16_fg_index_0_is_black),
      cmocka_unit_test(ansi16_fg_bright_differs_from_normal),
      cmocka_unit_test(ansi256_indices_0_to_7_map_to_fg),
      cmocka_unit_test(ansi256_indices_8_to_15_map_to_bright),
      cmocka_unit_test(ansi256_cube_index_16_is_black),
      cmocka_unit_test(ansi256_cube_index_231_is_max),
      cmocka_unit_test(ansi256_cube_pure_red),
      cmocka_unit_test(ansi256_cube_pure_green),
      cmocka_unit_test(ansi256_cube_pure_blue),
      cmocka_unit_test(ansi256_grey_base),
      cmocka_unit_test(ansi256_grey_top),
      cmocka_unit_test(ansi256_grey_step_is_10),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
