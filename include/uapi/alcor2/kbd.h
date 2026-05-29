/**
 * @file include/uapi/alcor2/kbd.h
 * @brief UAPI for the keyboard layout selector.
 *
 * Userland-visible bits: the layout enum and the two ioctls that switch
 * layouts or toggle key-release sentinels. Kernel-private translator decls
 * (@c kbd_read_translated, @c kbd_irq_check_intr, …) stay in
 * @c <alcor2/kbd.h>.
 */

#ifndef ALCOR2_UAPI_KBD_H
#define ALCOR2_UAPI_KBD_H

#include <stdint.h>

/*
 * Linux ioctl direction/size encoding (same as @c _IOW et al.):
 *   bits 31:30 — direction (01 = write, user → kernel)
 *   bits 23:16 — argument size in bytes
 *   bits 15:8  — type character ('K' = keyboard)
 *   bits  7:0  — command ordinal
 */

/**
 * @brief ioctl(0, ALCOR2_IOC_KBD_SET_LAYOUT, &uint32_t id)
 *
 * Mirrors musl @c _IOW('K', @c 1, @c uint32_t). @p id is one of
 * @c kbd_layout_t.
 */
#define ALCOR2_IOC_KBD_SET_LAYOUT                                              \
  ((1U << 30) | (0x4BU << 8) | 1U | (sizeof(uint32_t) << 16))

/**
 * @brief ioctl(0, ALCOR2_IOC_KBD_RELEASE_EVENTS, &uint32_t on)
 *
 * @p on @c != @c 0 enables @c \x00<char> sentinels on key-release; needed
 * for tracking multiple simultaneous keys (PS/2 typematic only repeats the
 * last key, breaking Z+D-style diagonal movement). Mirrors musl
 * @c _IOW('K', @c 2, @c uint32_t).
 */
#define ALCOR2_IOC_KBD_RELEASE_EVENTS                                          \
  ((1U << 30) | (0x4BU << 8) | 2U | (sizeof(uint32_t) << 16))

/** @brief Selectable keyboard layouts (argument for @c ALCOR2_IOC_KBD_SET_LAYOUT). */
typedef enum
{
  KBD_LAYOUT_US = 0, /**< US QWERTY. */
  KBD_LAYOUT_FR =
      1, /**< AZERTY lettering on a US scan map; US-ASCII digit row. */
  KBD_LAYOUT_COUNT
} kbd_layout_t;

#endif /* ALCOR2_UAPI_KBD_H */
