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

/*
 * Keyboard ioctl ABI (kernel side). Linux _IOW encoding, group 'K'.
 * Userland half lives in the musl fork's <sys/alcor_input.h>.
 *   bits 31:30 — direction (01 = write, user → kernel)
 *   bits 23:16 — argument size in bytes
 *   bits 15:8  — type character ('K' = keyboard)
 *   bits  7:0  — command ordinal
 */

/** ioctl(0, ALCOR2_IOC_KBD_SET_LAYOUT, &uint32_t id); id is a ::kbd_layout_t.
 */
#define ALCOR2_IOC_KBD_SET_LAYOUT                                              \
  ((1U << 30) | (0x4BU << 8) | 1U | (sizeof(uint32_t) << 16))

/** ioctl(0, ALCOR2_IOC_KBD_RELEASE_EVENTS, &uint32_t on); \x00<char> on key-up.
 */
#define ALCOR2_IOC_KBD_RELEASE_EVENTS                                          \
  ((1U << 30) | (0x4BU << 8) | 2U | (sizeof(uint32_t) << 16))

/** @brief Selectable keyboard layouts (argument for the SET_LAYOUT ioctl). */
typedef enum
{
  KBD_LAYOUT_US = 0, /**< US QWERTY. */
  KBD_LAYOUT_FR = 1, /**< AZERTY lettering on a US scan map; US-ASCII digits. */
  KBD_LAYOUT_COUNT
} kbd_layout_t;

struct proc;

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

/**
 * @brief Eager VINTR (and friends) detection for the keyboard IRQ.
 *
 * Called from @c keyboard_irq right after the new scancode is pushed.
 * Peeks the raw ring through the translator (state copy, no mutation) and
 * looks for a byte equal to the foreground proc's @c VINTR. On hit it
 * drains the ring through the real translator state up to that byte and
 * delivers @c SIGINT to the foreground PID.
 *
 * Bypassed entirely when no foreground PID is registered or its termios
 * has @c ISIG cleared, so the cost in the common case is two loads + a
 * compare.
 */
void kbd_irq_check_intr(void);

#endif /* ALCOR2_KBD_H */
