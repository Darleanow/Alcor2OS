/**
 * @file include/alcor2/kbd.h
 * @brief Keyboard layouts and tty-line translation (scancode → characters).
 *
 * Layout logic lives outside the PS/2 driver — similar to Linux kbd/console.
 */

#ifndef ALCOR2_KBD_H
#define ALCOR2_KBD_H

#include <alcor2/drivers/keyboard.h>
#include <alcor2/types.h>

/**
 * ioctl(request) for stdin (fd 0): set layout by id.
 *
 * Mirrors musl `_IOW('K', 1, uint32_t)` encoding.
 *
 * Usage (user): uint32_t id = KBD_LAYOUT_FR; ioctl(0, ALCOR2_IOC_KBD_SET_LAYOUT, &id);
 */
#define ALCOR2_IOC_KBD_SET_LAYOUT ((1U << 30) | (0x4BU << 8) | 1U | (sizeof(uint32_t) << 16))

typedef enum
{
  /** US QWERTY (traditional PC mapping). */
  KBD_LAYOUT_US = 0,
  /** AZERTY-style lettering on a USANSI-style scan map; digit row still US ASCII. */
  KBD_LAYOUT_FR = 1,
  KBD_LAYOUT_COUNT
} kbd_layout_t;

void kbd_set_layout(kbd_layout_t layout);
kbd_layout_t kbd_get_layout(void);

/** @brief Blocking read of layout-translated bytes (stdin / fd 0). */
u64 kbd_read_translated(char *buf, u64 count);

/**
 * True if more raw scancodes are buffered (used to block in read).
 */
bool kbd_raw_pending(void);

#endif /* ALCOR2_KBD_H */
