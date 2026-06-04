#include "test_common.h"
#include <alcor2/types.h>
#include <alcor2/drivers/fb_console.h>
#include <alcor2/drivers/console.h>
#include "../../src/kernel/drivers/fb_console/pixel.c"
#include <string.h>

static u8 mock_fb_mem[800 * 600 * 4];
fb_console_ctx_t fb_ctx;
// For tests expecting g_fb name
#define g_fb fb_ctx

static void setup_fb(u16 bpp) {
    g_fb.base = mock_fb_mem;
    g_fb.width = 800;
    g_fb.height = 600;
    g_fb.bytes_pp = bytes_pp_from_bpp(bpp);
    g_fb.pitch = 800 * g_fb.bytes_pp;
    memset(mock_fb_mem, 0, sizeof(mock_fb_mem));
}

static void test_fb_put_get_pixel_32(void **state) {
    (void)state;
    setup_fb(32);
    fb_put_pixel(10, 10, 0x123456);
    assert_int_equal(fb_get_pixel(10, 10), 0x123456);
}

static void test_fb_put_get_pixel_24(void **state) {
    (void)state;
    setup_fb(24);
    fb_put_pixel(10, 10, 0x123456);
    assert_int_equal(fb_get_pixel(10, 10), 0x123456);
}

static void test_fb_put_get_pixel_16(void **state) {
    (void)state;
    setup_fb(16);
    fb_put_pixel(10, 10, 0x123456);
    // 0x123456 -> R: 0x12, G: 0x34, B: 0x56
    // R: 0x12 >> 3 = 0x02; 0x02 << 3 = 0x10
    // G: 0x34 >> 2 = 0x0D; 0x0D << 2 = 0x34
    // B: 0x56 >> 3 = 0x0A; 0x0A << 3 = 0x50
    assert_int_equal(fb_get_pixel(10, 10), 0x103450);
}

static void test_fb_draw_glyph(void **state) {
    (void)state;
    setup_fb(32);
    u8 dummy_glyph[16] = {0};
    dummy_glyph[0] = 0x80; // 1st pixel lit (10000000)
    fb_draw_glyph(5, 5, dummy_glyph, 0xFF0000, 0x00FF00);
    assert_int_equal(fb_get_pixel(5, 5), 0xFF0000);
    assert_int_equal(fb_get_pixel(6, 5), 0x00FF00);
}

static void test_fb_draw_fallback_char(void **state) {
    (void)state;
    setup_fb(32);
    fb_draw_fallback_char(10, 10, 'A', 0xFFFFFF, 0x000000);
    // 'A' should draw at least some lit pixels and some bg pixels
    // Let's just verify it didn't crash and affected memory
    bool changed = false;
    for(int i = 0; i < 800*600*4; i++) {
        if (mock_fb_mem[i] != 0) {
            changed = true;
            break;
        }
    }
    assert_true(changed);
}

/* fb_put_pixel: no base → no-op (line 95) */
static void test_fb_put_pixel_no_base(void **state) {
  (void)state;
  fb_ctx.base = NULL;
  fb_put_pixel(0, 0, 0xFF0000); /* must not crash */
}

/* fb_put_pixel: out-of-bounds → no-op */
static void test_fb_put_pixel_oob(void **state) {
  (void)state;
  setup_fb(32);
  fb_put_pixel(800, 0, 0xFF0000); /* x == width → out-of-bounds */
  assert_int_equal(fb_get_pixel(799, 0), 0); /* unchanged */
}

/* fb_put_pixel: unknown bytes_pp → default: return (line 119) */
static void test_fb_put_pixel_unknown_bpp(void **state) {
  (void)state;
  setup_fb(32);
  fb_ctx.bytes_pp = 5; /* unsupported */
  fb_put_pixel(0, 0, 0xFF0000); /* hits default: return */
}

/* fb_get_pixel: no base → 0 (line 135) */
static void test_fb_get_pixel_no_base(void **state) {
  (void)state;
  fb_ctx.base = NULL;
  assert_int_equal(fb_get_pixel(0, 0), 0);
}

/* fb_get_pixel: unknown bytes_pp → default: return 0 (line 153) */
static void test_fb_get_pixel_unknown_bpp(void **state) {
  (void)state;
  setup_fb(32);
  fb_ctx.bytes_pp = 5;
  assert_int_equal(fb_get_pixel(0, 0), 0);
}

/* fb_draw_glyph: null glyph → no-op (line 172) */
static void test_fb_draw_glyph_null(void **state) {
  (void)state;
  setup_fb(32);
  fb_draw_glyph(0, 0, NULL, 0xFF0000, 0x000000); /* must not crash */
}

/* fb_draw_fallback_char: non-printable char → falls back to '?' (line 197) */
static void test_fb_draw_fallback_char_nonprintable(void **state) {
  (void)state;
  setup_fb(32);
  /* 0x01 is likely not in the atlas → font_glyph_index returns <0 → '?' */
  fb_draw_fallback_char(0, 0, '\x01', 0xFFFFFF, 0x000000);
  /* Just verify no crash — some pixels should be set */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fb_put_get_pixel_32),
        cmocka_unit_test(test_fb_put_get_pixel_24),
        cmocka_unit_test(test_fb_put_get_pixel_16),
        cmocka_unit_test(test_fb_draw_glyph),
        cmocka_unit_test(test_fb_draw_fallback_char),
        /* new coverage */
        cmocka_unit_test(test_fb_put_pixel_no_base),
        cmocka_unit_test(test_fb_put_pixel_oob),
        cmocka_unit_test(test_fb_put_pixel_unknown_bpp),
        cmocka_unit_test(test_fb_get_pixel_no_base),
        cmocka_unit_test(test_fb_get_pixel_unknown_bpp),
        cmocka_unit_test(test_fb_draw_glyph_null),
        cmocka_unit_test(test_fb_draw_fallback_char_nonprintable),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
