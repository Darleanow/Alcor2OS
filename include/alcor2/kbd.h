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

struct proc;

/*
 * Linux ioctl direction/size encoding (same as _IOW et al.):
 * bits 31:30 — direction (01 = write, user → kernel)
 * bits 23:16 — argument size in bytes
 * bits 15:8  — type character ('K' = keyboard)
 * bits  7:0  — command ordinal
 */

/**
 * ioctl(request) for stdin (fd 0): set layout by id.
 *
 * Mirrors musl `_IOW('K', 1, uint32_t)` encoding.
 *
 * Usage (user): uint32_t id = KBD_LAYOUT_FR; ioctl(0,
 * ALCOR2_IOC_KBD_SET_LAYOUT, &id);
 */
#define ALCOR2_IOC_KBD_SET_LAYOUT                                              \
  ((1U << 30) | (0x4BU << 8) | 1U | (sizeof(uint32_t) << 16))

/**
 * ioctl(request) for stdin (fd 0): toggle key-release events.
 *
 * When enabled, releasing a printable key emits a \x00<char> sentinel so apps
 * can track key-up precisely. Needed for simultaneous keys: PS/2 typematic
 * only repeats the last key pressed, so without this Z+D diagonal movement
 * breaks. Mirrors musl `_IOW('K', 2, uint32_t)`.
 *
 * Usage (user): uint32_t on = 1; ioctl(0, ALCOR2_IOC_KBD_RELEASE_EVENTS, &on);
 */
#define ALCOR2_IOC_KBD_RELEASE_EVENTS                                         \
  ((1U << 30) | (0x4BU << 8) | 2U | (sizeof(uint32_t) << 16))

/** @brief Selectable keyboard layouts. */
typedef enum
{
  KBD_LAYOUT_US = 0, /**< US QWERTY. */
  KBD_LAYOUT_FR = 1, /**< AZERTY lettering on a US scan map; US-ASCII digit row. */
  KBD_LAYOUT_COUNT
} kbd_layout_t;

/**
 * @brief Select the active layout.
 * @param layout One of ::kbd_layout_t; out-of-range falls back to US.
 */
void kbd_set_layout(kbd_layout_t layout);

/**
 * @brief Query the active layout.
 * @return The current ::kbd_layout_t.
 */
kbd_layout_t kbd_get_layout(void);

/**
 * @brief Toggle \x00<char> key-release sentinels.
 * @param enabled Non-zero to emit a sentinel on each printable key-up.
 */
void kbd_set_release_events(bool enabled);

/**
 * @brief Query whether release sentinels are enabled.
 * @return @c true if enabled.
 */
bool kbd_get_release_events(void);

/**
 * @brief Block until at least one layout-translated byte is available.
 * @param buf   Destination buffer.
 * @param count Capacity of @p buf in bytes.
 * @return Number of bytes written.
 */
u64 kbd_read_translated(char *buf, u64 count);

/**
 * @brief read(2) on stdin honoring the process's termios (ICANON, VMIN/VTIME).
 * @param p     Calling process (supplies the termios state).
 * @param buf   Destination buffer.
 * @param count Capacity of @p buf in bytes.
 * @return Number of bytes read, or a negative errno.
 */
u64 kbd_read_for_process(struct proc *p, char *buf, u64 count);

/**
 * @brief Whether a read(2) on fd 0 could return a byte right now.
 *
 * True when a translated byte (or a pending CSI sequence) is queued. A lone
 * key-up scancode does not count.
 * @return @c true if readable.
 */
bool kbd_raw_pending(void);

/**
 * @brief select(2) readability for fd 0, honoring ICANON line-ready semantics.
 * @param p Calling process (supplies the termios state).
 * @return @c true if fd 0 should be reported readable.
 */
bool kbd_select_read_ready(const struct proc *p);

#endif /* ALCOR2_KBD_H */
