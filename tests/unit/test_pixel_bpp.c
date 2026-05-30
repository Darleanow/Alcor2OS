/* Unit tests for bytes_pp_from_bpp() in
 * src/kernel/drivers/fb_console/pixel.c.
 * Pure switch function — no framebuffer state, no deps. */

#include "test_common.h"

#include <kernel/drivers/fb_console/internal.h>

/* fb_put_pixel references fb_ctx but we never call it — satisfy the linker. */
fb_console_ctx_t fb_ctx;

static void      bpp32_returns_4(void **state)
{
  (void)state;
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_32), FB_BYTES_PER_PIXEL_32);
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_32), 4);
}

static void bpp24_returns_3(void **state)
{
  (void)state;
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_24), FB_BYTES_PER_PIXEL_24);
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_24), 3);
}

static void bpp16_returns_2(void **state)
{
  (void)state;
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_16), FB_BYTES_PER_PIXEL_16);
  assert_int_equal(bytes_pp_from_bpp(FB_BPP_16), 2);
}

static void unknown_bpp_falls_back_to_32(void **state)
{
  (void)state;
  /* The default branch protects against unfamiliar bootloader modes. */
  assert_int_equal(bytes_pp_from_bpp(0), FB_BYTES_PER_PIXEL_32);
  assert_int_equal(bytes_pp_from_bpp(8), FB_BYTES_PER_PIXEL_32);
  assert_int_equal(bytes_pp_from_bpp(0xFFFF), FB_BYTES_PER_PIXEL_32);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(bpp32_returns_4),
      cmocka_unit_test(bpp24_returns_3),
      cmocka_unit_test(bpp16_returns_2),
      cmocka_unit_test(unknown_bpp_falls_back_to_32),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
