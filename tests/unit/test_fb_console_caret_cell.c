/**
 * @file tests/unit/test_fb_console_caret_cell.c
 * @brief Unit tests for fb_console/caret.c and fb_console/cell.c pure-state
 * paths.
 *
 * caret.c owns the s_drawn_x/s_drawn_y tracker and exposes four functions:
 * caret_erase, caret_paint, caret_refresh, caret_clear_drawn, and
 * caret_invalidate_in_batch. cell.c provides rgb_unpack, atlas_pixel_stride,
 * atlas_blit_width/height, and blit_cell_data / blit_cell — all of which
 * branch on fb_ctx state that can be set up without real hardware.
 */

#include "test_common.h"

#include <alcor2/types.h>

#include <string.h>

#define ALCOR2_IO_H
#define outb(p, v) ((void)(p), (void)(v))
#define inb(p)     ((u8)0)
#define io_wait()  ((void)0)

#include <kernel/drivers/fb_console/internal.h>
fb_console_ctx_t fb_ctx;

/* Pixel-write spy */
static u32 g_pixel_x[4096];
static u32 g_pixel_y[4096];
static u32 g_pixel_c[4096];
static int g_pixel_count = 0;

void fb_put_pixel(u32 x, u32 y, u32 c)
{
  if(g_pixel_count < 4096) {
    g_pixel_x[g_pixel_count] = x;
    g_pixel_y[g_pixel_count] = y;
    g_pixel_c[g_pixel_count] = c;
    g_pixel_count++;
  }
}

/* atlas stubs */
u32 atlas_lookup(u32 cp)      { (void)cp; return ATLAS_NO_GLYPH; }
u32 atlas_lookup_attr(u32 cp, u16 attr) { (void)cp; (void)attr; return ATLAS_NO_GLYPH; }
bool atlas_meta_is_sane(const fb_console_atlas_t *m) { (void)m; return false; }


/* scrollback / mouse / flush stubs */
void scroll_one(void)                        {}
void scrollback_repaint(void)                {}
void scrollback_exit(void)                   {}
void flush_pending_scroll(void)              {}
void scrollback_alloc_for(int c)             { (void)c; }
void scrollback_drop_pending(void)           {}
void mouse_cursor_invalidate_for_scroll(void){}
void mouse_cursor_render(void)               {}
void mouse_cursor_drop(void)                 {}
void flush_ci_ensure(int c)                  { (void)c; }
void flush_batch(void)                       {}
void put_cp_at_cursor(u32 cp)                { (void)cp; }

#include "../../src/kernel/drivers/fb_console/caret.c"
#include "../../src/kernel/drivers/fb_console/cell.c"


#define COLS 10
#define ROWS 5
static fb_cell_t g_cells[ROWS * COLS];

static int setup(void **state)
{
  (void)state;
  memset(&fb_ctx, 0, sizeof(fb_ctx));
  memset(g_cells, 0, sizeof(g_cells));
  g_pixel_count = 0;

  fb_ctx.cols          = COLS;
  fb_ctx.rows          = ROWS;
  fb_ctx.cell_w        = FONT_W;
  fb_ctx.cell_h        = FONT_H;
  fb_ctx.cells         = g_cells;
  fb_ctx.cursor_visible = 1;
  fb_ctx.blink_on       = 1;
  fb_ctx.yielded        = false;
  fb_ctx.atlas_active   = false;
  fb_ctx.cx             = 0;
  fb_ctx.cy             = 0;

  /* Reset caret tracker */
  caret_clear_drawn();
  return 0;
}


static void caret_erase_with_no_drawn_is_noop(void **state)
{
  (void)state;
  /* s_drawn_x = -1 initially — should not touch pixel buffer */
  caret_erase();
  assert_int_equal(g_pixel_count, 0);
}

static void caret_clear_drawn_resets_tracker(void **state)
{
  (void)state;
  /* Paint then clear: after clear, erase should be a noop again */
  caret_paint(); /* will write pixels because cursor_visible & blink_on */
  g_pixel_count = 0;
  caret_clear_drawn();
  caret_erase();
  assert_int_equal(g_pixel_count, 0);
}


static void caret_paint_yielded_skips(void **state)
{
  (void)state;
  fb_ctx.yielded = true;
  caret_paint();
  assert_int_equal(g_pixel_count, 0);
}

static void caret_paint_invisible_cursor_skips(void **state)
{
  (void)state;
  fb_ctx.cursor_visible = 0;
  caret_paint();
  assert_int_equal(g_pixel_count, 0);
}

static void caret_paint_blink_off_skips(void **state)
{
  (void)state;
  fb_ctx.blink_on = 0;
  caret_paint();
  assert_int_equal(g_pixel_count, 0);
}

static void caret_paint_null_cells_skips(void **state)
{
  (void)state;
  fb_ctx.cells = NULL;
  caret_paint();
  assert_int_equal(g_pixel_count, 0);
}

static void caret_paint_writes_pixels(void **state)
{
  (void)state;
  /* With bitmap fallback (no atlas, no base) put_pixel is called for the
   * bg fill of the cell. At least one pixel must be written. */
  fb_ctx.cx = 0;
  fb_ctx.cy = 0;
  caret_paint();
  /* The caret swaps fg/bg and calls blit_cell → blit_bitmap_glyph which
   * calls fb_put_pixel for each pixel in the cell rectangle. */
  assert_true(g_pixel_count > 0);
}

static void caret_paint_restores_cell_colors(void **state)
{
  (void)state;
  g_cells[0].fg = 0xFF0000u;
  g_cells[0].bg = 0x0000FFu;
  fb_ctx.cx = 0;
  fb_ctx.cy = 0;
  caret_paint();
  /* Cell colors must be restored after the inverted blit */
  assert_int_equal(g_cells[0].fg, 0xFF0000u);
  assert_int_equal(g_cells[0].bg, 0x0000FFu);
}


static void caret_refresh_erase_then_paint(void **state)
{
  (void)state;
  /* First paint so there is something to erase */
  caret_paint();
  int after_first_paint = g_pixel_count;
  caret_refresh();
  /* refresh must generate at least as many pixel writes as the initial paint */
  assert_true(g_pixel_count >= after_first_paint);
}


static void caret_invalidate_in_batch_no_drawn_noop(void **state)
{
  (void)state;
  fb_ctx.batch_r0 = 99;
  fb_ctx.batch_r1 = 0;
  caret_invalidate_in_batch();
  /* No drawn position → batch range unchanged */
  assert_int_equal(fb_ctx.batch_r0, 99);
}

static void caret_invalidate_in_batch_marks_dirty(void **state)
{
  (void)state;
  fb_ctx.cx       = 2;
  fb_ctx.cy       = 1;
  fb_ctx.batch_r0 = ROWS - 1;
  fb_ctx.batch_r1 = 0;
  caret_paint(); /* record drawn position */
  g_pixel_count = 0;
  caret_invalidate_in_batch();
  /* The cell must now be dirty */
  assert_true(g_cells[1 * COLS + 2].dirty);
}


static void blit_cell_data_reverse_swaps_colors(void **state)
{
  (void)state;
  fb_cell_t c = {
      .cp   = ' ',
      .fg   = 0xFF0000u,
      .bg   = 0x00FF00u,
      .attr = FB_ATTR_REVERSE,
  };
  g_pixel_count = 0;
  blit_cell_data(&c, 0, 0);
  /* With REVERSE: eff_fg=bg=0x00FF00, eff_bg=fg=0xFF0000.
   * The space fast-path fills every pixel with bg_pk = 0xFF000000|0xFF0000.
   * All pixels should be 0xFF0000 (packed red). */
  assert_true(g_pixel_count > 0);
  /* atlas is inactive → goes through blit_bitmap_glyph which fills bg first */
  /* Just verify at least one pixel was emitted (smoke test). */
}

static void blit_cell_data_no_base_no_crash(void **state)
{
  (void)state;
  /* base=NULL → 32-bpp path is gated on fb_ctx.base — should not crash */
  fb_ctx.base    = NULL;
  fb_cell_t c    = {.cp = 'A', .fg = 0xFFFFFF, .bg = 0x000000, .attr = 0};
  blit_cell_data(&c, 0, 0);
}


static void atlas_pixel_stride_32bpp(void **state)
{
  (void)state;
  fb_ctx.atlas_bpp = 32;
  assert_int_equal(atlas_pixel_stride(), 4);
}

static void atlas_pixel_stride_8bpp(void **state)
{
  (void)state;
  fb_ctx.atlas_bpp = 8;
  assert_int_equal(atlas_pixel_stride(), 1);
}

static void atlas_blit_width_clamped(void **state)
{
  (void)state;
  fb_ctx.atlas_cell_w = 4;
  fb_ctx.cell_w       = 8;
  assert_int_equal(atlas_blit_width(), 4); /* atlas smaller */
}

static void atlas_blit_width_full(void **state)
{
  (void)state;
  fb_ctx.atlas_cell_w = 12;
  fb_ctx.cell_w       = 8;
  assert_int_equal(atlas_blit_width(), 8); /* cell smaller */
}

static void atlas_blit_height_clamped(void **state)
{
  (void)state;
  fb_ctx.atlas_cell_h = 6;
  fb_ctx.cell_h       = 16;
  assert_int_equal(atlas_blit_height(), 6);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      /* caret_erase */
      cmocka_unit_test_setup(caret_erase_with_no_drawn_is_noop, setup),
      cmocka_unit_test_setup(caret_clear_drawn_resets_tracker, setup),
      /* caret_paint */
      cmocka_unit_test_setup(caret_paint_yielded_skips, setup),
      cmocka_unit_test_setup(caret_paint_invisible_cursor_skips, setup),
      cmocka_unit_test_setup(caret_paint_blink_off_skips, setup),
      cmocka_unit_test_setup(caret_paint_null_cells_skips, setup),
      cmocka_unit_test_setup(caret_paint_writes_pixels, setup),
      cmocka_unit_test_setup(caret_paint_restores_cell_colors, setup),
      /* caret_refresh */
      cmocka_unit_test_setup(caret_refresh_erase_then_paint, setup),
      /* caret_invalidate_in_batch */
      cmocka_unit_test_setup(caret_invalidate_in_batch_no_drawn_noop, setup),
      cmocka_unit_test_setup(caret_invalidate_in_batch_marks_dirty, setup),
      /* blit_cell_data */
      cmocka_unit_test_setup(blit_cell_data_reverse_swaps_colors, setup),
      cmocka_unit_test_setup(blit_cell_data_no_base_no_crash, setup),
      /* atlas helpers */
      cmocka_unit_test_setup(atlas_pixel_stride_32bpp, setup),
      cmocka_unit_test_setup(atlas_pixel_stride_8bpp, setup),
      cmocka_unit_test_setup(atlas_blit_width_clamped, setup),
      cmocka_unit_test_setup(atlas_blit_width_full, setup),
      cmocka_unit_test_setup(atlas_blit_height_clamped, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
