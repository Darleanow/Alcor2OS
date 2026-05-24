/**
 * @file include/alcor2/drivers/mouse.h
 * @brief Kernel-side mouse broker: accepts events from a low-level driver
 *        and serves them to userspace via /dev/mouse.
 */

#ifndef ALCOR2_DRIVERS_MOUSE_H
#define ALCOR2_DRIVERS_MOUSE_H

#include <alcor2/mouse.h>
#include <alcor2/types.h>

/** @brief Initialise the broker (ring buffer + waiter slot). */
void mouse_init(void);

/**
 * @brief Post one accumulated EV_SYN report.
 *
 * Called from the virtio-input IRQ handler once per @c EV_SYN. Updates the
 * cursor position (clamped in free mode, pinned at centre in relative mode),
 * pushes the packet into the ring, and wakes any blocked reader.
 */
void mouse_post_event(i32 dx, i32 dy, i16 dwheel, u8 buttons);

/** @brief Tell the broker the framebuffer geometry (for cursor clamping). */
void mouse_set_screen(u32 width, u32 height);

/** @brief Block until an event is available or a signal is pending. */
i64 mouse_read_block(alcor2_mouse_event_t *out);

/** @brief Toggle relative mode (cursor pinned to centre). */
void mouse_set_relative(bool enabled);

/** @brief Current mode. */
bool mouse_is_relative(void);

/** @brief Rendered cursor position. In relative mode, returns the centre. */
void mouse_get_cursor(i32 *out_x, i32 *out_y);

#endif
