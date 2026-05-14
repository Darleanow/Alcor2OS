/**
 * @file user/shell/include/vega/fb_tty.h
 * @brief Optional framebuffer text (FreeType + HarfBuzz) for the vega shell.
 *
 * When active, all shell stdout (and relayed child output) goes through a
 * mmap framebuffer terminal: same Fira face, cell grid, and CSI parsing as a
 * minimal xterm. Children fork with @c sh_fb_tty_on_fork_child() so they write
 * bytes to a pipe; the parent feeds those bytes here.
 */

#ifndef VEGA_FB_TTY_H
#define VEGA_FB_TTY_H

#include <stdbool.h>

/**
 * @brief Load font, mmap framebuffer, enable FB text path.
 * @param font_path TTF path (e.g. /bin/FiraCode-Regular.ttf).
 * @return false if fb syscalls, mmap, or font load fails (shell keeps bitmap).
 */
bool sh_fb_tty_init(const char *font_path);

bool sh_fb_tty_active(void);

/**
 * @brief Call in the child immediately after fork.
 *
 * The child inherits the parent's @e active flag; without this, @c sh_puts
 * would render into the framebuffer instead of fd 1 (pipe/kernel).
 */
void sh_fb_tty_on_fork_child(void);

void sh_fb_tty_shutdown(void);

void sh_fb_tty_puts(const char *s);
void sh_fb_tty_putchar(unsigned char c);
/** Reshape all dirty rows without yielding — for interactive single-char
 * output. */
void sh_fb_tty_flush(void);
/** Reshape dirty rows then yield so KVM/QEMU scans VRAM before the next read().
 * Use after batch writes. */
void sh_fb_tty_present(void);
void sh_fb_tty_clear(void);

/** Toggle blink phase and redraw the text cursor (call when input idle, e.g.
 * select timeout). */
void sh_fb_tty_cursor_poll(void);

/** Flip the A_BLINK phase and repaint affected rows. Call from any idle hook
 * (line-edit select timeout and child-pipe-relay poll timeout both do).
 * Self-contained around the line-edit cursor: the bar is hidden across the
 * row repaints and re-drawn at the end if cursor_poll is currently in the
 * "on" half of its blink. */
void sh_fb_tty_blink_tick(void);

/** Show the block cursor at the current cell and set blink phase to "on" before
 * reading stdin (after @c select, or after line edits). */
void sh_fb_tty_cursor_suspend(void);

/** After line editing output, redraw the block cursor at the current cell
 * (TTY-style). */
void sh_fb_tty_cursor_after_edit(void);

#endif
