/**
 * @file include/alcor2/keyboard.h
 * @brief PS/2 keyboard driver.
 *
 * Scancode-to-ASCII translation and input buffering for user programs.
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
 * @brief Current modifier key state.
 */
typedef struct
{
  bool shift;
  bool ctrl;
  bool alt;
  bool capslock;
} key_state_t;

/**
 * @brief Keyboard event handler callback.
 * @param c ASCII character (or 0 if non-printable).
 * @param scancode Raw scancode.
 * @param state Modifier key state.
 */
typedef void (*keyboard_handler_t)(char c, u8 scancode, key_state_t state);

/**
 * @brief Initialize the PS/2 keyboard driver.
 */
void keyboard_init(void);

/**
 * @brief Set the keyboard event handler.
 * @param handler Callback function.
 */
void keyboard_set_handler(keyboard_handler_t handler);

/**
 * @brief Convert scancode to ASCII character.
 * @param scancode PS/2 scancode.
 * @param shift True if shift is pressed.
 * @return ASCII character or 0 if non-printable.
 */
char keyboard_scancode_to_char(u8 scancode, bool shift);

/**
 * @brief Read buffered keyboard input (for SYS_READ).
 * @param buf Destination buffer.
 * @param count Bytes to read.
 * @return Bytes read.
 */
u64 keyboard_read(char *buf, u64 count);

/**
 * @brief Check if keyboard buffer has data.
 * @return true if data is available.
 */
bool keyboard_has_data(void);

#endif
