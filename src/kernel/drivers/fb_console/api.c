/**
 * @file src/kernel/drivers/fb_console/api.c
 * @brief Runtime framebuffer text console with UTF-8 and ANSI/CSI support.
 *
 * Maintains an in-RAM cell grid, blits glyphs via the compiled-in CP437 bitmap
 * or a userspace-supplied Fira atlas, and renders scrollback history.
 * Takes over the framebuffer from the early boot logger once kmalloc is up.
 */

#include <alcor2/drivers/fb_console.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/types.h>
#include <drivers/console/font.h>
#include <kernel/drivers/fb_console/internal.h>

/* Forward declaration — avoids pulling in proc/signal.h for one call. */
#define SIGWINCH 28
void             proc_signal_broadcast(int signum);

fb_console_ctx_t fb_ctx;

/**
 * @brief Populate @c fb_ctx with the geometry from Limine + the boot-time
 * defaults (CP437 cell size, zero margin, Catppuccin palette).
 *
 * Pulled out of @ref fb_console_init so the init function's "did the kmalloc
 * succeed" branch is the only failure path to read; the field assignments
 * cannot fail and clutter that decision when inlined.
 *
 * @param fb      MMIO base of the framebuffer.
 * @param width   Framebuffer width in pixels.
 * @param height  Framebuffer height in pixels.
 * @param pitch   Bytes per scanline.
 * @param bpp     Bits per pixel.
 */
static void
    fb_console_init_ctx(void *fb, u64 width, u64 height, u64 pitch, u16 bpp)
{
  fb_ctx.base       = fb;
  fb_ctx.width      = width;
  fb_ctx.height     = height;
  fb_ctx.pitch      = pitch;
  fb_ctx.bytes_pp   = bytes_pp_from_bpp(bpp);
  fb_ctx.cell_w     = FONT_W;
  fb_ctx.cell_h     = FONT_H;
  fb_ctx.margin_x   = 0;
  fb_ctx.margin_y   = 0;
  fb_ctx.cols       = (int)(width / (u64)fb_ctx.cell_w);
  fb_ctx.rows       = (int)(height / (u64)fb_ctx.cell_h);
  fb_ctx.default_fg = CATPPUCCIN_MOCHA_TEXT;
  fb_ctx.default_bg = CATPPUCCIN_MOCHA_BASE;
  fb_ctx.cur_fg     = fb_ctx.default_fg;
  fb_ctx.cur_bg     = fb_ctx.default_bg;
  fb_ctx.cx = fb_ctx.cy = 0;
  fb_ctx.utf8_rem       = 0;
  fb_ctx.blink_ticks    = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on       = 1;
  fb_ctx.cell_blink_on  = 1;
  fb_ctx.cur_attr       = 0;
  fb_ctx.cursor_visible = 1;
  fb_ctx.yielded        = false;
  fb_ctx.in_head = fb_ctx.in_tail = 0;
}

/**
 * @brief Allocate the cell grid and prime every cell with the default empty
 * state.
 *
 * Single failure point so callers can collapse "no grid, no rendering" into
 * a NULL check elsewhere; on failure the rest of the renderer is forced into
 * its no-op path.
 *
 * @return @c true on success, @c false on kmalloc failure.
 */
static bool fb_console_alloc_grid(void)
{
  size_t total = (size_t)fb_ctx.rows * (size_t)fb_ctx.cols;
  fb_ctx.cells = kmalloc(total * sizeof(fb_cell_t));
  if(!fb_ctx.cells)
    return false;
  for(size_t i = 0; i < total; i++) {
    fb_ctx.cells[i].cp    = ' ';
    fb_ctx.cells[i].fg    = fb_ctx.default_fg;
    fb_ctx.cells[i].bg    = fb_ctx.default_bg;
    fb_ctx.cells[i].attr  = 0;
    fb_ctx.cells[i].dirty = 0;
  }
  return true;
}

/**
 * @brief Bring the framebuffer console online and allocate its cell grid.
 *
 * Sized to fill the framebuffer with the bitmap font (8x16); the grid is
 * resized once a userspace atlas registers a different cell size. Margins
 * stay zero on this path so boot output uses every pixel — the margin is
 * an atlas-era cosmetic, not a fundamental layout property.
 *
 * @param fb      MMIO base of the framebuffer (kernel-mapped pointer).
 * @param width   Framebuffer width in pixels.
 * @param height  Framebuffer height in pixels.
 * @param pitch   Bytes per scanline (may exceed @p width * bytes-per-pixel).
 * @param bpp     Bits per pixel; normalised to 1/2/3/4 bytes-per-pixel.
 * @return @c true on success, @c false if the cell grid kmalloc failed
 *         (in which case every subsequent fb_console_* call no-ops).
 */
bool fb_console_init(void *fb, u64 width, u64 height, u64 pitch, u16 bpp)
{
  fb_console_init_ctx(fb, width, height, pitch, bpp);
  if(!fb_console_alloc_grid())
    return false;
  scrollback_alloc_for(fb_ctx.cols);
  flush_ci_ensure(fb_ctx.cols);
  return true;
}

/**
 * @brief Open a writev-style batch: subsequent feed bytes go through the
 * dirty-cell pipeline rather than blitting per byte.
 *
 * Snaps out of scrollback first (any write implies "user wants the live
 * view back"), then invalidates the painted caret so the batched repaint
 * also clears the inverted block — without this the caret leaks through
 * fast output.
 */
void fb_console_write_begin(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  scrollback_exit();
  fb_ctx.batch_r0 = fb_ctx.rows;
  fb_ctx.batch_r1 = -1;
  caret_invalidate_in_batch();
  fb_ctx.in_batch = true;
}

/**
 * @brief Feed @p len bytes through the ANSI/UTF-8 parser without flushing.
 *
 * Must be inside an open @ref fb_console_write_begin / @ref
 * fb_console_write_end pair; called by writev to fan multiple iovecs into
 * one paint pass.
 *
 * @param buf  Bytes to feed.
 * @param len  Byte count.
 */
void fb_console_write_raw(const void *buf, size_t len)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  const u8 *p = buf;
  for(size_t i = 0; i < len; i++)
    feed_byte(p[i]);
}

/**
 * @brief Close a batch: flush queued pixel-scrolls, repaint every dirty cell
 * in one VRAM pass, then repaint the caret on top.
 *
 * Blink-on is reset so the caret is visible immediately after fast output —
 * matches user expectation that typing-and-getting-no-response is a bug.
 */
void fb_console_write_end(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  fb_ctx.in_batch = false;
  flush_pending_scroll();
  flush_batch();
  fb_ctx.blink_ticks = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on    = 1;
  caret_paint();
}

/**
 * @brief Convenience: begin + raw + end as one call.
 *
 * The default path for a single @c write(2) — userspace code that doesn't
 * care about batching doesn't need to know batching exists.
 *
 * @param buf  Bytes to write.
 * @param len  Byte count.
 */
void fb_console_write(const void *buf, size_t len)
{
  fb_console_write_begin();
  fb_console_write_raw(buf, len);
  fb_console_write_end();
}

/**
 * @brief Push one byte from the keyboard IRQ into the read ring.
 *
 * Drops on overflow rather than blocking: the IRQ path must not stall. The
 * blink phase is forced visible so the cursor stays solid while the user is
 * actively typing (visible-cursor lag while typing is a known UX problem
 * with raw blink).
 *
 * @param byte  Byte produced by the keyboard layer (UTF-8 / xterm seq).
 */
void fb_console_push_input(u8 byte)
{
  unsigned int next = (fb_ctx.in_tail + 1u) % INPUT_RING;
  if(next == fb_ctx.in_head)
    return;
  fb_ctx.in_buf[fb_ctx.in_tail] = byte;
  fb_ctx.in_tail                = next;
  fb_ctx.blink_ticks            = FB_BLINK_PERIOD_TICKS;
  fb_ctx.blink_on               = 1;
}

/**
 * @brief Drain up to @p max bytes from the input ring.
 *
 * Stops at ring-empty rather than blocking; callers handle the partial-read
 * semantics so a poll loop can interleave with other fds.
 *
 * @param buf  Destination buffer.
 * @param max  Capacity.
 * @return Bytes copied (0..max).
 */
size_t fb_console_read(void *buf, size_t max)
{
  u8    *out = buf;
  size_t n   = 0;
  while(n < max && fb_ctx.in_head != fb_ctx.in_tail) {
    out[n++]       = fb_ctx.in_buf[fb_ctx.in_head];
    fb_ctx.in_head = (fb_ctx.in_head + 1u) % INPUT_RING;
  }
  return n;
}

/**
 * @brief Mark every blinking cell dirty and update the dirty-row range.
 *
 * Called once per blink half-period so SGR-blink cells get repainted with the
 * new phase. Whole-grid scan is intentional — the renderer doesn't track a
 * separate "blink cells" set; bytes saved per cell outweigh the cost of the
 * occasional full sweep.
 */
static void mark_blink_cells_dirty(void)
{
  fb_ctx.batch_r0 = fb_ctx.rows;
  fb_ctx.batch_r1 = -1;
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++) {
      fb_cell_t *cell =
          &fb_ctx.cells[(size_t)r * (size_t)fb_ctx.cols + (size_t)c];
      if(!(cell->attr & FB_ATTR_BLINK))
        continue;
      cell->dirty = 1;
      if(r < fb_ctx.batch_r0)
        fb_ctx.batch_r0 = r;
      if(r > fb_ctx.batch_r1)
        fb_ctx.batch_r1 = r;
    }
}

/**
 * @brief Flip the blink phase and repaint every cell that depends on it.
 *
 * Repaint is batched (one VRAM pass for every blinking cell) and the mouse
 * cursor is dropped beforehand because the same scanlines are about to be
 * overwritten — the next @ref mouse_cursor_render redraws cleanly.
 */
static void blink_tick(void)
{
  fb_ctx.blink_on      = !fb_ctx.blink_on;
  fb_ctx.cell_blink_on = fb_ctx.blink_on;
  fb_ctx.blink_ticks   = FB_BLINK_PERIOD_TICKS;
  mark_blink_cells_dirty();
  if(fb_ctx.batch_r0 <= fb_ctx.batch_r1) {
    flush_batch();
    mouse_cursor_drop();
  }
  caret_refresh();
}

/**
 * @brief PIT tick callback: advance blink phase and reposition the mouse
 * cursor.
 *
 * Two concerns share the tick because both want the same cadence: caret
 * blink at the configured rate, mouse cursor smoothness as fast as the tick
 * itself. The blink branch repaints @ref FB_ATTR_BLINK cells in one batched
 * flush so the screen doesn't flicker; mouse render early-outs when the
 * pointer hasn't moved, so it's free at rest.
 */
void fb_console_tick(void)
{
  if(fb_ctx.yielded || !fb_ctx.cells)
    return;
  if(fb_ctx.blink_ticks == 0)
    blink_tick();
  else
    fb_ctx.blink_ticks--;
  mouse_cursor_render();
}

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
  *new_cols = (int)((fb_ctx.width - MARGIN_SIDES_COUNT * FB_CONSOLE_MARGIN) /
                    (u64)new_cell_w);
  *new_rows = (int)((fb_ctx.height - MARGIN_SIDES_COUNT * FB_CONSOLE_MARGIN) /
                    (u64)new_cell_h);
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

/**
 * @brief Read the live cell grid dimensions.
 *
 * Out-parameters because the cell grid is two values; returning a struct
 * would force every caller to introduce a name. NULL pointers are silently
 * skipped, matching what TIOCGWINSZ and similar query paths need.
 *
 * @param cols  Out: column count (may be NULL).
 * @param rows  Out: row count (may be NULL).
 */
void fb_console_get_size(int *cols, int *rows)
{
  if(cols)
    *cols = fb_ctx.cols;
  if(rows)
    *rows = fb_ctx.rows;
}

/**
 * @brief Pack live console geometry into a Linux winsize for a TIOCGWINSZ
 * answer.
 *
 * Pre-seeded with the VT100 fallback and re-clamped after the query because
 * @ref fb_console_get_size returns the raw @c ctx fields without sanitising —
 * if a TTY ioctl ever races the boot path before @ref fb_console_init runs we
 * still hand userspace something a TUI can divide by.
 *
 * Pixel dimensions are zeroed: the grid is a character matrix, no caller has
 * a use for the underlying pixel span and reporting a wrong value would mislead
 * TUIs into laying out against a non-existent geometry.
 *
 * @param out  Destination winsize. Caller-owned, no NULL guard (a kernel
 *             caller passing NULL is a logic bug — fail fast via crash).
 */
void fb_console_fill_winsize(k_winsize_t *out)
{
  int cols = KTERM_WINSIZE_FALLBACK_COLS;
  int rows = KTERM_WINSIZE_FALLBACK_ROWS;
  fb_console_get_size(&cols, &rows);
  if(cols <= 0)
    cols = KTERM_WINSIZE_FALLBACK_COLS;
  if(rows <= 0)
    rows = KTERM_WINSIZE_FALLBACK_ROWS;
  out->row    = rows;
  out->col    = cols;
  out->xpixel = 0;
  out->ypixel = 0;
}

/**
 * @brief Report DECCKM state to the keyboard layer.
 *
 * Exposed (rather than letting kbd read @c fb_ctx directly) so the kbd
 * layer doesn't gain a dependency on the fb_console internal layout —
 * cursor-key encoding depends on this one bit and nothing else.
 *
 * @return @c true when CSI cursor keys should switch to SS3 form.
 */
bool fb_console_app_cursor_keys(void)
{
  return fb_ctx.app_cursor_keys;
}

/**
 * @brief Hand the framebuffer to a userspace mmap-er (typically Doom).
 *
 * Subsequent writes from the kernel are no-oped (yielded gate) so the
 * user-program's pixels survive. The mouse cursor is dropped so the next
 * @ref fb_console_reclaim repaints a fresh arrow rather than trying to
 * XOR-erase a position whose underlying pixels have been replaced.
 */
void fb_console_yield(void)
{
  fb_ctx.yielded = true;
  mouse_cursor_drop();
}

/**
 * @brief Resume kernel rendering after a @ref fb_console_yield, fully
 * repainting the framebuffer.
 *
 * Background-fills the full FB first so margin pixels left behind by the
 * userspace owner (Doom paints inside the cell grid but the margin pixels
 * are out-of-band) get cleaned. Then blits every cell so the full text
 * grid is back in sync.
 */
void fb_console_reclaim(void)
{
  fb_ctx.yielded = false;
  mouse_cursor_drop();
  if(!fb_ctx.cells)
    return;
  if(fb_ctx.base && fb_ctx.bytes_pp == FB_BYTES_PER_PIXEL_32)
    for(u32 y = 0; y < fb_ctx.height; y++)
      fill32(
          (volatile u32 *)(fb_ctx.base + (u64)y * fb_ctx.pitch),
          BGRA_OPAQUE_ALPHA | fb_ctx.default_bg, fb_ctx.width
      );
  scrollback_drop_pending();
  for(int r = 0; r < fb_ctx.rows; r++)
    for(int c = 0; c < fb_ctx.cols; c++)
      blit_cell(c, r);
  caret_clear_drawn();
  caret_paint();
}
