/**
 * @file include/alcor2/drivers/mouse.h
 * @brief Kernel-side mouse broker: accepts events from a low-level driver
 *        and serves them to userspace via /dev/mouse.
 */

#ifndef ALCOR2_DRIVERS_MOUSE_H
#define ALCOR2_DRIVERS_MOUSE_H

#include <alcor2/types.h>

/** @brief One mouse event delivered via @c read(/dev/mouse). */
typedef struct alcor2_mouse_event
{
  i32 dx;      /**< Relative X delta since last EV_SYN. */
  i32 dy;      /**< Relative Y delta since last EV_SYN. */
  i16 dwheel;  /**< Vertical wheel notches (positive = up). */
  u8  buttons; /**< Bitmask: bit 0 LEFT, bit 1 RIGHT, bit 2 MIDDLE. */
  u8  flags;   /**< Reserved; zero today. */
} alcor2_mouse_event_t;

/** @brief Button bit positions in @c alcor2_mouse_event.buttons. */
#define ALCOR2_MOUSE_BTN_LEFT   0x01
#define ALCOR2_MOUSE_BTN_RIGHT  0x02
#define ALCOR2_MOUSE_BTN_MIDDLE 0x04

/* ioctl ABI (kernel side); Linux _IOR/_IOW, group 'M'. Userland half lives in
 * the musl fork's <sys/alcor_input.h>. */
#define ALCOR2_MOUSE_IOC_DIR_W 1U
#define ALCOR2_MOUSE_IOC_DIR_R 2U
#define ALCOR2_MOUSE_IOC_MAKE(dir, size, nr)                                   \
  (((dir) << 30) | ((u32)(size) << 16) | ('M' << 8) | (nr))

/** ioctl(fd, ALCOR2_IOC_MOUSE_SET_RELATIVE, &uint32_t enabled). */
#define ALCOR2_IOC_MOUSE_SET_RELATIVE                                          \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_W, 4, 1)

/** ioctl(fd, ALCOR2_IOC_MOUSE_GET_RELATIVE, &uint32_t out). */
#define ALCOR2_IOC_MOUSE_GET_RELATIVE                                          \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_R, 4, 2)

/** ioctl(fd, ALCOR2_IOC_MOUSE_GET_POS, &{i32 x, i32 y}). */
#define ALCOR2_IOC_MOUSE_GET_POS                                               \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_R, 8, 3)

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
 * @param buttons Button bitmask (see ALCOR2_MOUSE_BTN_*).
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
i64 mouse_read_block(alcor2_mouse_event_t *out);

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
