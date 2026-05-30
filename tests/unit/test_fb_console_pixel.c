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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fb_put_get_pixel_32),
        cmocka_unit_test(test_fb_put_get_pixel_24),
        cmocka_unit_test(test_fb_put_get_pixel_16),
        cmocka_unit_test(test_fb_draw_glyph),
        cmocka_unit_test(test_fb_draw_fallback_char),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
