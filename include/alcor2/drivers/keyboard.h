/**
 * @file include/alcor2/drivers/keyboard.h
 * @brief PS/2 keyboard driver — raw scancode ring buffer.
 *
 * Layout (scancode → characters) is handled in @ref kbd.h / `kbd_layout.c`, not here.
 */

#ifndef ALCOR2_KEYBOARD_H
#define ALCOR2_KEYBOARD_H

#include <alcor2/types.h>

/** @brief Release bit in scancode. */
#define KEY_RELEASE 0x80

/** @name Scancode constants
 * @{ */
#define KEY_ESC       0x01
#define KEY_BACKSPACE 0x0E
#define KEY_TAB       0x0F
#define KEY_ENTER     0x1C
#define KEY_LCTRL     0x1D
#define KEY_LSHIFT    0x2A
#define KEY_RSHIFT    0x36
#define KEY_LALT      0x38
#define KEY_CAPSLOCK  0x3A
#define KEY_F1        0x3B
#define KEY_F2        0x3C
#define KEY_F3        0x3D
#define KEY_F4        0x3E
#define KEY_F5        0x3F
#define KEY_F6        0x40
#define KEY_F7        0x41
#define KEY_F8        0x42
#define KEY_F9        0x43
#define KEY_F10       0x44
#define KEY_F11       0x57
#define KEY_F12       0x58
/** @} */

/**
 * @brief Current modifier key state (for tty-style translation layers).
 */
typedef struct
{
  bool shift;
  bool ctrl;
  bool alt;
  bool capslock;
} key_state_t;

void keyboard_init(void);

/** @brief IRQ path: push raw PS/2 byte onto ring buffer. */
void keyboard_irq(void);

bool keyboard_raw_available(void);
u8   keyboard_raw_pop(void);

#endif
