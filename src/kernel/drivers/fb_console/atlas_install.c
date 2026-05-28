/**
 * @file src/kernel/drivers/fb_console/atlas_install.c
 * @brief Userspace atlas submission pipeline for the framebuffer console.
 *
 * Implements @ref fb_console_set_atlas and its phases: validate descriptor →
 * mirror user buffers into kernel copies → reflow the cell grid when cell
 * geometry changes → repaint + broadcast @c SIGWINCH. Lives in its own file
 * because each phase has its own failure mode that needs an unambiguous
 * return path, which crowded @c api.c when inlined.
 */

#include <alcor2/drivers/fb_console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/** @brief Linux SIGWINCH number. Forward-declared so we don't pull in
 * @c <proc/signal.h> for a single broadcast call. */
#define SIGWINCH 28

/** @brief Wake every process so TUIs re-query @c TIOCGWINSZ after a reflow.
 * Defined in @c proc/signal.c. */
void proc_signal_broadcast(int signum);

/**
 * @brief Validate @p meta and the user buffers it points at.
 *
 * Kept separate from the install step so a bad submission fails fast without
 * any allocation churn. Both halves of validation (logical caps via
 * @ref atlas_meta_is_sane, and address-range checks via vmm) must pass.
 *
 * @param meta      Atlas descriptor copied from userspace.
 * @param cp_bytes  Out: size of the codepoint map in bytes, for the install
 *                  step to reuse.
 * @return @c true if the descriptor is safe to copy; @c false otherwise.
 */
static bool validate_atlas_meta(const fb_console_atlas_t *meta, u64 *cp_bytes)
{
  if(!meta || !atlas_meta_is_sane(meta))
    return false;
  if(!vmm_is_user_range((void *)(u64)meta->pixels_user, meta->pixels_size))
    return false;
  *cp_bytes = (u64)meta->n_cp * sizeof(u32);
  if(!vmm_is_user_range((void *)(u64)meta->cp_map_user, *cp_bytes))
    return false;
  return true;
}

/**
 * @brief Mirror the user-side atlas into kernel-owned buffers and publish
 *        every field of @p meta into @c fb_ctx.atlas_*.
 *
 * The copy is what lets the kernel keep rendering after the submitting
 * process @c munmap's its source — without it, every cell blit would race
 * the unmap. Previous atlas storage is kfree'd here so a reload doesn't
 * leak memory across submissions.
 *
 * @param meta      Validated descriptor.
 * @param cp_bytes  Size of the codepoint map (from @ref validate_atlas_meta).
 * @return 0 on success, -1 if either copy buffer kmalloc failed (state
 *         unchanged in that case).
 */
static int install_atlas_payload(const fb_console_atlas_t *meta, u64 cp_bytes)
{
  u8  *new_pixels = kmalloc(meta->pixels_size);
  u32 *new_cp_map = kmalloc(cp_bytes);
  if(!new_pixels || !new_cp_map) {
    if(new_pixels)
      kfree(new_pixels);
    if(new_cp_map)
      kfree(new_cp_map);
    return -1;
  }
  /* Current proc's CR3 is active during the syscall: user VAs are mapped. */
  kmemcpy(new_pixels, (const void *)(u64)meta->pixels_user, meta->pixels_size);
  kmemcpy(new_cp_map, (const void *)(u64)meta->cp_map_user, cp_bytes);

  if(fb_ctx.atlas_pixels)
    kfree(fb_ctx.atlas_pixels);
  if(fb_ctx.atlas_cp_map)
    kfree(fb_ctx.atlas_cp_map);

  fb_ctx.atlas_pixels      = new_pixels;
  fb_ctx.atlas_cp_map      = new_cp_map;
  fb_ctx.atlas_cell_w      = meta->cell_w;
  fb_ctx.atlas_cell_h      = meta->cell_h;
  fb_ctx.atlas_stride      = meta->stride_bytes;
  fb_ctx.atlas_bpp         = meta->bpp;
  fb_ctx.atlas_n_glyphs    = meta->n_glyphs;
  fb_ctx.atlas_n_cp        = meta->n_cp;
  fb_ctx.atlas_fallback    = meta->fallback_idx;
  fb_ctx.atlas_bold_base   = meta->bold_offset;
  fb_ctx.atlas_italic_base = meta->italic_offset;
  fb_ctx.atlas_active      = true;
  return 0;
}

/**
 * @brief Compute the new column/row counts that fit @p new_cell_w/h pixels
 * inside the framebuffer with @ref FB_CONSOLE_MARGIN on each side.
 *
 * Clamped to at least 1x1 so a comically large atlas doesn't produce a
 * degenerate empty grid that breaks the renderer's bounds assumptions.
 *
 * @param new_cell_w  Cell width in pixels.
 * @param new_cell_h  Cell height in pixels.
 * @param new_cols    Out: column count.
 * @param new_rows    Out: row count.
 */
static void compute_grid_dims(
    int new_cell_w, int new_cell_h, int *new_cols, int *new_rows
)
{
  u64 margin_px = (u64)MARGIN_SIDES_COUNT * (u64)FB_CONSOLE_MARGIN;
  *new_cols     = (int)((fb_ctx.width - margin_px) / (u64)new_cell_w);
  *new_rows     = (int)((fb_ctx.height - margin_px) / (u64)new_cell_h);
  if(*new_cols < 1)
    *new_cols = 1;
  if(*new_rows < 1)
    *new_rows = 1;
}

/**
 * @brief Allocate a fresh empty grid sized @p cols x @p rows and replace
 *        @c fb_ctx.cells with it.
 *
 * Pre-existing cells are dropped — cell coordinates have no meaning across a
 * column reflow. On kmalloc failure the existing grid stays in place; the
 * caller (still inside the atlas install path) keeps rendering with the old
 * geometry rather than crashing.
 *
 * @param cols  New column count.
 * @param rows  New row count.
 * @return @c true on success, @c false on allocation failure.
 */
static bool swap_grid_for_dims(int cols, int rows)
{
  size_t     total = (size_t)cols * (size_t)rows;
  fb_cell_t *nc    = kmalloc(total * sizeof(fb_cell_t));
  if(!nc)
    return false;
  for(size_t i = 0; i < total; i++) {
    nc[i].cp   = ' ';
    nc[i].fg   = fb_ctx.default_fg;
    nc[i].bg   = fb_ctx.default_bg;
    nc[i].attr = 0;
  }
  if(fb_ctx.cells)
    kfree(fb_ctx.cells);
  fb_ctx.cells = nc;
  return true;
}

/**
 * @brief Bg-fill the whole framebuffer (margins included) at the current
 *        default bg colour.
 *
 * Used by @ref reflow_grid_for_atlas after a cell-size change so margin
 * pixels that fell outside the new grid get cleared — without it, residue
 * from the previous geometry would survive at the edges.
 */
static void clear_framebuffer_to_default_bg(void)
{
  for(u32 y = 0; y < fb_ctx.height; y++)
    for(u32 x = 0; x < fb_ctx.width; x++)
      fb_put_pixel(x, y, fb_ctx.default_bg);
}

/**
 * @brief Reflow the cell grid when the new atlas's cell size differs from
 *        the live one.
 *
 * No-op when the cell geometry matches what's already on screen (atlas reload
 * with same metrics). On grid resize, the previous cell content is discarded —
 * cell coordinates don't survive a cols/rows change in any well-defined way.
 * On kmalloc failure the existing grid is left in place and the atlas blit
 * clips to the old @c cell_w/cell_h instead.
 *
 * @param meta  Validated descriptor.
 */
static void reflow_grid_for_atlas(const fb_console_atlas_t *meta)
{
  int new_cell_w = (int)meta->cell_w;
  int new_cell_h = (int)meta->cell_h;
  int new_marg_x = FB_CONSOLE_MARGIN;
  int new_marg_y = FB_CONSOLE_MARGIN;
  if(new_cell_w == fb_ctx.cell_w && new_cell_h == fb_ctx.cell_h &&
     new_marg_x == fb_ctx.margin_x && new_marg_y == fb_ctx.margin_y)
    return;

  int new_cols, new_rows;
  compute_grid_dims(new_cell_w, new_cell_h, &new_cols, &new_rows);
  if(!swap_grid_for_dims(new_cols, new_rows))
    return;

  fb_ctx.cols     = new_cols;
  fb_ctx.rows     = new_rows;
  fb_ctx.cell_w   = new_cell_w;
  fb_ctx.cell_h   = new_cell_h;
  fb_ctx.margin_x = new_marg_x;
  fb_ctx.margin_y = new_marg_y;
  fb_ctx.cx = fb_ctx.cy = 0;
  fb_ctx.saved_cx = fb_ctx.saved_cy = 0;
  scrollback_alloc_for(new_cols);
  flush_ci_ensure(new_cols);
  clear_framebuffer_to_default_bg();
}

/**
 * @brief Repaint the whole grid and wake every TUI when the geometry changed.
 *
 * @c SIGWINCH is sent only when the grid dimensions actually changed; a same-
 * size reload doesn't need to disturb any TUI's redraw state.
 *
 * @param old_cols  Column count captured before @ref reflow_grid_for_atlas.
 * @param old_rows  Row count captured before @ref reflow_grid_for_atlas.
 */
static void repaint_and_notify_winsize(int old_cols, int old_rows)
{
  scrollback_drop_pending();
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      blit_cell(c, r);
  caret_clear_drawn();
  caret_paint();
  if(fb_ctx.cols != old_cols || fb_ctx.rows != old_rows)
    proc_signal_broadcast(SIGWINCH);
}

/**
 * @brief Register a userspace glyph atlas and reflow the cell grid to its
 *        cell size.
 *
 * Pipeline: validate descriptor → mirror user buffers into kernel copies →
 * reflow the grid if cell geometry changed → repaint + broadcast
 * @c SIGWINCH. Each phase is its own helper so the failure cases (validation
 * fails, copy OOM, reflow OOM) have unambiguous return paths.
 *
 * @param meta  Atlas descriptor.
 * @return 0 on success, -1 on any validation or installation failure.
 */
int fb_console_set_atlas(const fb_console_atlas_t *meta)
{
  u64 cp_bytes;
  if(!validate_atlas_meta(meta, &cp_bytes))
    return -1;
  if(install_atlas_payload(meta, cp_bytes) < 0)
    return -1;

  int old_cols = fb_ctx.cols;
  int old_rows = fb_ctx.rows;
  reflow_grid_for_atlas(meta);
  repaint_and_notify_winsize(old_cols, old_rows);
  return 0;
}
