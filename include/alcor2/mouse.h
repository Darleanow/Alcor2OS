/**
 * @file include/alcor2/mouse.h
 * @brief Shared user/kernel ABI for /dev/mouse events and mode control.
 */

#ifndef ALCOR2_MOUSE_H
#define ALCOR2_MOUSE_H

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

/* ioctl encoding mirrors Linux _IOR/_IOW (see alcor2/kbd.h for the convention).
 */
#define ALCOR2_MOUSE_IOC_DIR_W 1U
#define ALCOR2_MOUSE_IOC_DIR_R 2U
#define ALCOR2_MOUSE_IOC_MAKE(dir, size, nr)                                   \
  (((dir) << 30) | ((u32)(size) << 16) | ('M' << 8) | (nr))

/** ioctl(fd, ALCOR2_IOC_MOUSE_SET_RELATIVE, &uint32_t enabled).
 *  enabled != 0 → relative mode (cursor pinned, deltas flow). */
#define ALCOR2_IOC_MOUSE_SET_RELATIVE                                          \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_W, 4, 1)

/** ioctl(fd, ALCOR2_IOC_MOUSE_GET_RELATIVE, &uint32_t out). */
#define ALCOR2_IOC_MOUSE_GET_RELATIVE                                          \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_R, 4, 2)

/** ioctl(fd, ALCOR2_IOC_MOUSE_GET_POS, &{i32 x, i32 y}).
 *  Returns the current rendered cursor position. In relative mode this is
 *  the centre (screen_w/2, screen_h/2). */
#define ALCOR2_IOC_MOUSE_GET_POS                                               \
  ALCOR2_MOUSE_IOC_MAKE(ALCOR2_MOUSE_IOC_DIR_R, 8, 3)

#endif
