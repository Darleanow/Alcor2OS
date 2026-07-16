/**
 * @file include/alcor2/drivers/mouse.h
 * @brief Kernel-side mouse broker: accepts events from a low-level driver
 *        and serves them to userspace via /dev/mouse.
 */

#ifndef ALCOR2_DRIVERS_MOUSE_H
#define ALCOR2_DRIVERS_MOUSE_H

#include <alcor2/types.h>
#include <bits/alcor_input.h>

/** @brief Initialise the broker: clear the ring, cursor, and waiter slot. */
void mouse_init(void);

/**
 * @brief Initialise the PS/2 mouse on the i8042 second port.
 *
 * Enables the AUX port and IRQ 12 and starts data reporting; decoded packets
 * reach the broker via ::mouse_post_event.
 * @return @c true if a mouse acknowledged, @c false otherwise.
 */
bool mouse_ps2_init(void);

/**
 * @brief Post one movement/button report into the broker.
 *
 * Called from the mouse IRQ handler (interrupts already off). Updates the
 * cursor (clamped to the screen in free mode, pinned to centre in relative
 * mode), enqueues the event, and wakes any blocked reader.
 * @param dx      X delta (right positive).
 * @param dy      Y delta (down positive).
 * @param dwheel  Wheel delta (up positive).
 * @param buttons Button bitmask (see ALCOR_MOUSE_BTN_*).
 */
void mouse_post_event(i32 dx, i32 dy, i16 dwheel, u8 buttons);

/**
 * @brief Set the screen geometry used to clamp the cursor.
 * @param width  Screen width in pixels.
 * @param height Screen height in pixels.
 */
void mouse_set_screen(u32 width, u32 height);

/**
 * @brief Block until one event is available, then dequeue it.
 * @param out Destination event.
 * @return 0 on success, or a negative errno.
 */
i64 mouse_read_block(alcor_mouse_event_t *out);

/**
 * @brief Enable or disable relative mode (cursor pinned to centre, raw deltas
 *        passed through). Drains the ring on every mode change.
 * @param enabled Non-zero for relative mode.
 */
void mouse_set_relative(bool enabled);

/**
 * @brief Query the current mode.
 * @return @c true if relative mode is active.
 */
bool mouse_is_relative(void);

/**
 * @brief Get the rendered cursor position (the centre in relative mode).
 * @param out_x Receives the X coordinate (may be NULL).
 * @param out_y Receives the Y coordinate (may be NULL).
 */
void mouse_get_cursor(i32 *out_x, i32 *out_y);

/**
 * @brief Whether the pointer has moved at least once since boot.
 *
 * Lets the console keep the cursor hidden until first use.
 * @return @c true once any non-zero motion has been seen.
 */
bool mouse_has_moved(void);

#endif
