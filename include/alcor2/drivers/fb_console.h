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

#include <alcor2/fb_console_ioctl.h>
#include <alcor2/types.h>
#include <stdbool.h>
#include <stddef.h>

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
 * @brief Open a multi-buffer write batch. Call before feeding multiple
 * discontiguous buffers (e.g. writev). Suppresses per-buffer pixel flushes.
 */
void fb_console_write_begin(void);

/**
 * @brief Feed @p len bytes in an open batch (no flush). Must be preceded by
 * @ref fb_console_write_begin.
 */
void fb_console_write_raw(const void *buf, size_t len);

/**
 * @brief Close the batch opened by @ref fb_console_write_begin: flush dirty
 * cells, scroll pixels, and repaint the cursor. One call per writev.
 */
void fb_console_write_end(void);

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

/* fb_console_atlas_t and ioctl constants are in <alcor2/fb_console_ioctl.h>. */

/** @brief Register a userspace glyph atlas; subsequent renders use Fira. */
int fb_console_set_atlas(const fb_console_atlas_t *meta);

/**
 * @brief Yield the framebuffer to a userspace process for raw pixel access
 * (games, graphics demos). The kernel stops rendering until @ref
 * fb_console_reclaim is called.
 */
void fb_console_yield(void);

/** @brief Resume kernel rendering after @ref fb_console_yield; repaints the
 * full cell grid. */
void fb_console_reclaim(void);

/** @brief Cell grid dimensions in cells (not pixels). Both pointers may be
 * NULL. Used by TIOCGWINSZ so userspace TUIs lay out against the real grid. */
void fb_console_get_size(int *cols, int *rows);

/** @brief DECCKM state. When true the keyboard layer emits SS3 (@c \\EOA)
 * for cursor keys instead of CSI (@c \\E[A). Toggled by ncurses keypad(). */
bool fb_console_app_cursor_keys(void);

/** @brief Scroll the scrollback view up by @p lines. No-op if no history. */
void fb_console_scrollback_up(int lines);

/** @brief Scroll the scrollback view down by @p lines; 0 = live view. */
void fb_console_scrollback_down(int lines);

#endif /* ALCOR2_FB_CONSOLE_H */
