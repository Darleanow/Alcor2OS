/**
 * @file include/alcor2/drivers/fb_console.h
 * @brief Kernel framebuffer text console.
 *
 * Runtime terminal that takes over the framebuffer once kmalloc is up.
 * Maintains an in-RAM text cell grid, parses UTF-8 + ANSI/CSI sequences, blits
 * glyphs via either the compiled-in CP437 bitmap or a userspace-supplied Fira
 * atlas (registered via @c FB_CONSOLE_SET_ATLAS).
 *
 * The early boot logger in @c src/drivers/console/console.c stays for messages
 * emitted before @ref fb_console_init runs.
 */

#ifndef ALCOR2_FB_CONSOLE_H
#define ALCOR2_FB_CONSOLE_H

#include <alcor2/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ioctls on fd 1/2 — Linux-style _IOC encoding so userspace can use a normal
 * ioctl(2) call. Group byte is 'F' for "framebuffer console". */

/** SET_ATLAS: userspace submits a glyph atlas. arg = fb_console_atlas_t*. */
#define FB_CONSOLE_SET_ATLAS                                                   \
  ((1U << 30) | ((unsigned)'F' << 8) | 1U | (sizeof(fb_console_atlas_t) << 16))

/** YIELD: release the framebuffer for raw mmap use (doom). arg ignored. */
#define FB_CONSOLE_YIELD ((unsigned)('F' << 8) | 2U)

/** RECLAIM: resume kernel rendering, repaint the grid. arg ignored. */
#define FB_CONSOLE_RECLAIM ((unsigned)('F' << 8) | 3U)

/**
 * @brief Initialise the runtime console using the same framebuffer the boot
 * logger has been writing to. Allocates the cell grid via kmalloc, so call
 * after heap init.
 *
 * @param fb         Linear framebuffer base.
 * @param width      Pixels per row.
 * @param height     Pixels per column.
 * @param pitch      Bytes per scanline.
 * @param bpp        Bits per pixel (16/24/32).
 * @return true on success, false if allocation fails.
 */
bool fb_console_init(void *fb, u64 width, u64 height, u64 pitch, u16 bpp);

/**
 * @brief Feed @p len bytes into the terminal. Parses UTF-8 + ANSI/CSI; renders
 * to the framebuffer. Safe to call from interrupt context only if the caller
 * has serialised access — there is no internal locking for now.
 */
void fb_console_write(const void *buf, size_t len);

/**
 * @brief Read one input byte from the keyboard layer into the console's
 * input ring. Called from the keyboard IRQ. Plain pass-through queueing — the
 * kernel keyboard already handles line discipline + layout translation.
 */
void fb_console_push_input(u8 byte);

/**
 * @brief Drain up to @p max bytes of pending input into @p buf.
 *
 * @return Bytes copied; 0 if the ring is empty.
 */
size_t fb_console_read(void *buf, size_t max);

/**
 * @brief Tick the cursor blink phase. Called from the PIT IRQ (100 Hz).
 *
 * Internally divides by ~50 → ~2 Hz blink. Any write or input keystroke resets
 * the phase to "on" so the cursor stays visible while the user is interacting.
 */
void fb_console_tick(void);

/**
 * @brief Atlas descriptor submitted by userspace.
 *
 * The kernel maps @c pixels_user and @c cp_map_user read-only into kernel
 * space (so it sees what userspace put there) and keeps the mapping alive
 * for the atlas's lifetime.
 */
typedef struct
{
  uint64_t pixels_user;  /**< userspace VA of the glyph atlas pixel data. */
  uint32_t pixels_size;  /**< total atlas bytes. */
  uint32_t cell_w;       /**< glyph cell width in pixels. */
  uint32_t cell_h;       /**< glyph cell height in pixels. */
  uint32_t stride_bytes; /**< bytes per row of a single cell. */
  uint32_t bpp;          /**< atlas bpp — must match framebuffer. */
  uint32_t n_glyphs;     /**< total glyph slots in the atlas. */
  uint64_t cp_map_user; /**< userspace VA of u32[n_cp] codepoint → glyph_idx. */
  uint32_t n_cp;        /**< size of cp_map (covers codepoints 0..n_cp-1). */
  uint32_t fallback_idx; /**< glyph for unmapped codepoints. */
} fb_console_atlas_t;

/** @brief Register a userspace glyph atlas; subsequent renders use Fira. */
int fb_console_set_atlas(const fb_console_atlas_t *meta);

/**
 * @brief Release the framebuffer so a userspace process can mmap it raw
 * (doom, graphics demos). The kernel stops rendering until @ref
 * fb_console_reclaim is called.
 */
/** Cell grid dimensions in cells (not pixels). Both pointers may be NULL.
 *  Used by TIOCGWINSZ so userspace TUIs lay out against the real grid. */
void fb_console_get_size(int *cols, int *rows);

/** DECCKM state. When true, the keyboard layer should emit SS3 (`\EOA`)
 *  for cursor keys instead of CSI (`\E[A`). Toggled by ncurses keypad mode. */
bool fb_console_app_cursor_keys(void);

void fb_console_yield(void);

/** @brief Resume kernel rendering; repaint the cell grid. */
void fb_console_reclaim(void);

#endif /* ALCOR2_FB_CONSOLE_H */
