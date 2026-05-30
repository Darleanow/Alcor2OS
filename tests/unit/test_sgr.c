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

static int       g_params[32];
static int       g_nparams;
int              csi_params(int *pv, int maxn)
{
  int n = g_nparams < maxn ? g_nparams : maxn;
  for(int i = 0; i < n; i++)
    pv[i] = g_params[i];
  return n;
}

void fb_put_pixel(u32 x, u32 y, u32 c)
{
  (void)x;
  (void)y;
  (void)c;
}

#include "../../src/kernel/drivers/fb_console/palette.c"
#include "../../src/kernel/drivers/fb_console/sgr.c"

#define DEFAULT_FG 0xBBBBBBu
#define DEFAULT_BG 0x000000u

static int setup(void **state)
{
  (void)state;
  memset(&fb_ctx, 0, sizeof(fb_ctx));
  fb_ctx.default_fg = DEFAULT_FG;
  fb_ctx.default_bg = DEFAULT_BG;
  fb_ctx.cur_fg     = DEFAULT_FG;
  fb_ctx.cur_bg     = DEFAULT_BG;
  fb_ctx.cur_attr   = 0;
  g_nparams         = 0;
  return 0;
}

static void sgr_reset_clears_attrs_and_colours(void **state)
{
  (void)state;
  fb_ctx.cur_attr = FB_ATTR_BOLD | FB_ATTR_ITALIC;
  fb_ctx.cur_fg   = 0xFF0000u;
  g_nparams       = 1;
  g_params[0]     = SGR_RESET;
  csi_sgr();
  assert_int_equal(fb_ctx.cur_attr, 0);
  assert_int_equal(fb_ctx.cur_fg, DEFAULT_FG);
  assert_int_equal(fb_ctx.cur_bg, DEFAULT_BG);
}

static void sgr_empty_params_is_reset(void **state)
{
  (void)state;
  fb_ctx.cur_attr = FB_ATTR_BOLD;
  g_nparams       = 0;
  csi_sgr();
  assert_int_equal(fb_ctx.cur_attr, 0);
}

static void sgr_bold_sets_bit(void **state)
{
  (void)state;
  assert_true(sgr_apply_attr(SGR_BOLD));
  assert_true(fb_ctx.cur_attr & FB_ATTR_BOLD);
}

static void sgr_italic_sets_bit(void **state)
{
  (void)state;
  assert_true(sgr_apply_attr(SGR_ITALIC));
  assert_true(fb_ctx.cur_attr & FB_ATTR_ITALIC);
}

static void sgr_underline_sets_bit(void **state)
{
  (void)state;
  assert_true(sgr_apply_attr(SGR_UNDERLINE));
  assert_true(fb_ctx.cur_attr & FB_ATTR_UNDERLINE);
}

static void sgr_no_bold_clears_bit(void **state)
{
  (void)state;
  fb_ctx.cur_attr = FB_ATTR_BOLD;
  assert_true(sgr_apply_attr(SGR_NO_BOLD));
  assert_false(fb_ctx.cur_attr & FB_ATTR_BOLD);
}

static void sgr_unknown_attr_returns_false(void **state)
{
  (void)state;
  assert_false(sgr_apply_attr(999));
}

static void sgr_fg_base_sets_colour(void **state)
{
  (void)state;
  assert_true(sgr_apply_basic_color(SGR_FG_BASE));
  assert_int_equal(fb_ctx.cur_fg, ansi16_fg[0]);
}

static void sgr_fg_end_sets_colour(void **state)
{
  (void)state;
  assert_true(sgr_apply_basic_color(SGR_FG_END));
  assert_int_equal(fb_ctx.cur_fg, ansi16_fg[SGR_FG_END - SGR_FG_BASE]);
}

static void sgr_bg_base_sets_colour(void **state)
{
  (void)state;
  assert_true(sgr_apply_basic_color(SGR_BG_BASE));
  assert_int_equal(fb_ctx.cur_bg, ansi16_bg[0]);
}

static void sgr_fg_default_restores(void **state)
{
  (void)state;
  fb_ctx.cur_fg = 0xFF0000u;
  assert_true(sgr_apply_basic_color(SGR_FG_DEFAULT));
  assert_int_equal(fb_ctx.cur_fg, DEFAULT_FG);
}

static void sgr_bg_default_restores(void **state)
{
  (void)state;
  fb_ctx.cur_bg = 0xFF0000u;
  assert_true(sgr_apply_basic_color(SGR_BG_DEFAULT));
  assert_int_equal(fb_ctx.cur_bg, DEFAULT_BG);
}

static void sgr_bright_fg_sets_colour(void **state)
{
  (void)state;
  assert_true(sgr_apply_basic_color(SGR_FG_BRIGHT_BASE));
  assert_int_equal(fb_ctx.cur_fg, ansi16_fg_bright[0]);
}

static void sgr_unknown_colour_returns_false(void **state)
{
  (void)state;
  assert_false(sgr_apply_basic_color(20));
}

static void sgr_extended_256_fg(void **state)
{
  (void)state;
  int pv[] = {SGR_FG_EXTENDED, SGR_EXT_FORM_256, 196};
  int pi   = 0;
  assert_true(sgr_apply_extended_color(pv, 3, &pi));
  assert_int_equal(fb_ctx.cur_fg, ansi256_to_rgb(196));
  assert_int_equal(pi, 2);
}

static void sgr_extended_256_bg(void **state)
{
  (void)state;
  int pv[] = {SGR_BG_EXTENDED, SGR_EXT_FORM_256, 21};
  int pi   = 0;
  assert_true(sgr_apply_extended_color(pv, 3, &pi));
  assert_int_equal(fb_ctx.cur_bg, ansi256_to_rgb(21));
}

static void sgr_extended_truecolor_fg(void **state)
{
  (void)state;
  int pv[] = {SGR_FG_EXTENDED, SGR_EXT_FORM_TRUECOLOR, 0xFF, 0x80, 0x00};
  int pi   = 0;
  assert_true(sgr_apply_extended_color(pv, 5, &pi));
  assert_int_equal(fb_ctx.cur_fg, 0xFF8000u);
  assert_int_equal(pi, 4);
}

static void sgr_extended_too_few_params_returns_false(void **state)
{
  (void)state;
  int pv[] = {SGR_FG_EXTENDED, SGR_EXT_FORM_256};
  int pi   = 0;
  assert_false(sgr_apply_extended_color(pv, 2, &pi));
}

static void sgr_extended_unknown_form_returns_false(void **state)
{
  (void)state;
  int pv[] = {SGR_FG_EXTENDED, 99, 0};
  int pi   = 0;
  assert_false(sgr_apply_extended_color(pv, 3, &pi));
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(sgr_reset_clears_attrs_and_colours, setup),
      cmocka_unit_test_setup(sgr_empty_params_is_reset, setup),
      cmocka_unit_test_setup(sgr_bold_sets_bit, setup),
      cmocka_unit_test_setup(sgr_italic_sets_bit, setup),
      cmocka_unit_test_setup(sgr_underline_sets_bit, setup),
      cmocka_unit_test_setup(sgr_no_bold_clears_bit, setup),
      cmocka_unit_test_setup(sgr_unknown_attr_returns_false, setup),
      cmocka_unit_test_setup(sgr_fg_base_sets_colour, setup),
      cmocka_unit_test_setup(sgr_fg_end_sets_colour, setup),
      cmocka_unit_test_setup(sgr_bg_base_sets_colour, setup),
      cmocka_unit_test_setup(sgr_fg_default_restores, setup),
      cmocka_unit_test_setup(sgr_bg_default_restores, setup),
      cmocka_unit_test_setup(sgr_bright_fg_sets_colour, setup),
      cmocka_unit_test_setup(sgr_unknown_colour_returns_false, setup),
      cmocka_unit_test_setup(sgr_extended_256_fg, setup),
      cmocka_unit_test_setup(sgr_extended_256_bg, setup),
      cmocka_unit_test_setup(sgr_extended_truecolor_fg, setup),
      cmocka_unit_test_setup(sgr_extended_too_few_params_returns_false, setup),
      cmocka_unit_test_setup(sgr_extended_unknown_form_returns_false, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
